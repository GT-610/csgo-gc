#include "stdafx.h"
#include "saved_item_shuffles.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace SavedItemShuffles
{

namespace
{

constexpr uint32_t OriginalAppId = 730;
constexpr uint64_t WriteCallPrefix = 0x4353474f00000000ULL;
constexpr uint64_t ReadCallPrefix = 0x4353475200000000ULL;
constexpr uint64_t WriteCallPrefixMask = 0xffffffff00000000ULL;
constexpr uint64_t WriteCallSuccessMask = 1;
constexpr size_t MaxPendingReadCalls = 16;
constexpr size_t MaxPendingReadBytes = k_unMaxCloudFileChunkSize;

std::atomic<uint32_t> s_temporaryFileCounter;
std::atomic<uint32_t> s_writeCallCounter{ 1 };
std::atomic<uint32_t> s_readCallCounter{ 1 };

struct ReadCall
{
    EResult result{ k_EResultFail };
    uint32 offset{};
    uint32 requestedSize{};
    std::vector<uint8_t> data;
};

std::mutex s_readCallsMutex;
std::unordered_map<SteamAPICall_t, ReadCall> s_readCalls;
std::deque<SteamAPICall_t> s_readCallOrder;
size_t s_pendingReadBytes;

bool SeekFile(FILE *file, int64 offset, int origin)
{
#ifdef _WIN32
    return _fseeki64(file, offset, origin) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), origin) == 0;
#endif
}

int64 TellFile(FILE *file)
{
#ifdef _WIN32
    return _ftelli64(file);
#else
    return static_cast<int64>(ftello(file));
#endif
}

void EraseReadCallLocked(SteamAPICall_t call)
{
    auto it = s_readCalls.find(call);
    if (it == s_readCalls.end())
    {
        return;
    }

    s_pendingReadBytes -= it->second.data.size();
    s_readCalls.erase(it);
    auto orderIt = std::find(s_readCallOrder.begin(), s_readCallOrder.end(), call);
    if (orderIt != s_readCallOrder.end())
    {
        s_readCallOrder.erase(orderIt);
    }
}

void StoreReadCallLocked(SteamAPICall_t call, ReadCall readCall)
{
    while (!s_readCallOrder.empty()
        && (s_readCalls.size() >= MaxPendingReadCalls
            || s_pendingReadBytes + readCall.data.size() > MaxPendingReadBytes))
    {
        SteamAPICall_t oldest = s_readCallOrder.front();
        s_readCallOrder.pop_front();
        auto it = s_readCalls.find(oldest);
        if (it != s_readCalls.end())
        {
            s_pendingReadBytes -= it->second.data.size();
            s_readCalls.erase(it);
        }
    }

    s_pendingReadBytes += readCall.data.size();
    s_readCalls.emplace(call, std::move(readCall));
    s_readCallOrder.push_back(call);
}

bool PathsEqual(const char *left, const char *right)
{
    if (!left || !right)
    {
        return false;
    }

    while (*left && *right)
    {
        unsigned char a = static_cast<unsigned char>(*left++);
        unsigned char b = static_cast<unsigned char>(*right++);
        if (std::tolower(a) != std::tolower(b))
        {
            return false;
        }
    }

    return *left == *right;
}

void PrintFileError(const char *operation)
{
    Platform::Print("Saved item shuffles: %s %s failed (%d: %s)\n",
        operation, LocalPath, errno, std::strerror(errno));
}

bool EnsureDirectoryExists()
{
#ifdef _WIN32
    if (CreateDirectoryA(DirectoryPath, nullptr))
    {
        return true;
    }

    DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS)
    {
        DWORD attributes = GetFileAttributesA(DirectoryPath);
        if (attributes != INVALID_FILE_ATTRIBUTES
            && (attributes & FILE_ATTRIBUTE_DIRECTORY))
        {
            return true;
        }
    }

    Platform::Print("Saved item shuffles: creating %s failed (Windows error %lu)\n",
        DirectoryPath, static_cast<unsigned long>(error));
    return false;
#else
    if (mkdir(DirectoryPath, 0755) == 0)
    {
        return true;
    }

    if (errno == EEXIST)
    {
        struct stat status{};
        if (stat(DirectoryPath, &status) == 0 && S_ISDIR(status.st_mode))
        {
            return true;
        }
    }

    Platform::Print("Saved item shuffles: creating %s failed (%d: %s)\n",
        DirectoryPath, errno, std::strerror(errno));
    return false;
#endif
}

std::string TemporaryPath()
{
    std::string path = LocalPath;
    path.append(".tmp.");
#ifdef _WIN32
    path.append(std::to_string(_getpid()));
#else
    path.append(std::to_string(getpid()));
#endif
    path.push_back('.');
    path.append(std::to_string(
        s_temporaryFileCounter.fetch_add(1, std::memory_order_relaxed)));
    return path;
}

bool ReplaceFile(const char *source, const char *destination)
{
#ifdef _WIN32
    if (MoveFileExA(source, destination,
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        return true;
    }

    Platform::Print("Saved item shuffles: replacing %s failed (Windows error %lu)\n",
        destination, static_cast<unsigned long>(GetLastError()));
    return false;
#else
    if (rename(source, destination) == 0)
    {
        return true;
    }

    PrintFileError("replacing");
    return false;
#endif
}

} // namespace

bool ShouldUseLocalStorage(uint32_t appId, const char *remotePath)
{
    return appId != OriginalAppId && PathsEqual(remotePath, RemotePath);
}

bool FileWrite(const void *data, uint32_t size)
{
    if (!data || size > k_unMaxCloudFileChunkSize)
    {
        return false;
    }

    if (!EnsureDirectoryExists())
    {
        return false;
    }

    std::string temporaryPath = TemporaryPath();
    FILE *file = fopen(temporaryPath.c_str(), "wb");
    if (!file)
    {
        PrintFileError("opening");
        return false;
    }

    bool succeeded = fwrite(data, 1, size, file) == size;
    succeeded &= !ferror(file) && fflush(file) == 0;
#ifdef _WIN32
    if (succeeded)
    {
        succeeded = _commit(_fileno(file)) == 0;
    }
#else
    if (succeeded)
    {
        succeeded = fsync(fileno(file)) == 0;
    }
#endif
    succeeded &= fclose(file) == 0;

    if (!succeeded)
    {
        PrintFileError("writing");
        remove(temporaryPath.c_str());
        return false;
    }

    if (!ReplaceFile(temporaryPath.c_str(), LocalPath))
    {
        remove(temporaryPath.c_str());
        return false;
    }

    return true;
}

int32 FileRead(void *data, int32 size)
{
    if (!data || size <= 0)
    {
        return 0;
    }

    errno = 0;
    FILE *file = fopen(LocalPath, "rb");
    if (!file)
    {
        if (errno != ENOENT)
        {
            PrintFileError("opening");
        }
        return 0;
    }

    size_t bytesRead = fread(data, 1, static_cast<size_t>(size), file);
    bool failed = ferror(file) != 0 || fclose(file) != 0;
    if (failed)
    {
        PrintFileError("reading");
        return 0;
    }

    return static_cast<int32>(bytesRead);
}

bool FileForget()
{
    // There is no cloud copy to forget. Keep the local file, matching Steam's
    // behavior of removing cloud persistence without deleting local data.
    return FileExists();
}

bool FileDelete()
{
    errno = 0;
    if (remove(LocalPath) == 0)
    {
        return true;
    }

    if (errno != ENOENT)
    {
        PrintFileError("deleting");
    }
    return false;
}

bool FileExists()
{
    errno = 0;
    FILE *file = fopen(LocalPath, "rb");
    if (!file)
    {
        if (errno != ENOENT)
        {
            PrintFileError("opening");
        }
        return false;
    }

    if (fclose(file) != 0)
    {
        PrintFileError("closing");
        return false;
    }

    return true;
}

bool FilePersisted()
{
    return FileExists();
}

int32 GetFileSize()
{
    errno = 0;
    FILE *file = fopen(LocalPath, "rb");
    if (!file)
    {
        if (errno != ENOENT)
        {
            PrintFileError("opening");
        }
        return 0;
    }

    bool failed = fseek(file, 0, SEEK_END) != 0;
    long size = failed ? -1 : ftell(file);
    failed |= size < 0 || size > (std::numeric_limits<int32>::max)();
    failed |= fclose(file) != 0;
    if (failed)
    {
        PrintFileError("measuring");
        return 0;
    }

    return static_cast<int32>(size);
}

int64 GetFileTimestamp()
{
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExA(LocalPath, GetFileExInfoStandard, &attributes))
    {
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
        {
            Platform::Print("Saved item shuffles: getting timestamp for %s failed (Windows error %lu)\n",
                LocalPath, static_cast<unsigned long>(error));
        }
        return 0;
    }

    ULARGE_INTEGER timestamp{};
    timestamp.LowPart = attributes.ftLastWriteTime.dwLowDateTime;
    timestamp.HighPart = attributes.ftLastWriteTime.dwHighDateTime;
    constexpr uint64_t WindowsToUnixEpoch = 116444736000000000ULL;
    if (timestamp.QuadPart < WindowsToUnixEpoch)
    {
        return 0;
    }
    return static_cast<int64>((timestamp.QuadPart - WindowsToUnixEpoch) / 10000000ULL);
#else
    struct stat status{};
    if (stat(LocalPath, &status) != 0)
    {
        if (errno != ENOENT)
        {
            PrintFileError("getting timestamp for");
        }
        return 0;
    }
    return static_cast<int64>(status.st_mtime);
#endif
}

SteamAPICall_t MakeWriteCall(bool success)
{
    uint64_t sequence = s_writeCallCounter.fetch_add(1, std::memory_order_relaxed);
    sequence &= 0x7fffffffULL;
    return WriteCallPrefix | (sequence << 1) | (success ? WriteCallSuccessMask : 0);
}

bool IsWriteCall(SteamAPICall_t call)
{
    return (call & WriteCallPrefixMask) == WriteCallPrefix;
}

bool WriteCallSucceeded(SteamAPICall_t call)
{
    return IsWriteCall(call) && (call & WriteCallSuccessMask) != 0;
}

bool GetWriteCallResult(SteamAPICall_t call, void *callback, int callbackSize,
    int expectedCallback, bool *failed)
{
    if (!IsWriteCall(call)
        || !callback
        || callbackSize != sizeof(RemoteStorageFileWriteAsyncComplete_t)
        || expectedCallback != RemoteStorageFileWriteAsyncComplete_t::k_iCallback)
    {
        if (failed)
        {
            *failed = true;
        }
        return false;
    }

    if (failed)
    {
        *failed = false;
    }

    RemoteStorageFileWriteAsyncComplete_t result{};
    result.m_eResult = WriteCallSucceeded(call) ? k_EResultOK : k_EResultFail;
    memcpy(callback, &result, sizeof(result));
    return true;
}

SteamAPICall_t MakeReadCall(uint32 offset, uint32 size)
{
    uint64_t sequence = s_readCallCounter.fetch_add(1, std::memory_order_relaxed);
    SteamAPICall_t call = ReadCallPrefix | sequence;

    ReadCall readCall;
    readCall.offset = offset;
    readCall.requestedSize = size;
    if (size > k_unMaxCloudFileChunkSize)
    {
        readCall.result = k_EResultInvalidParam;
    }
    else
    {
        errno = 0;
        FILE *file = fopen(LocalPath, "rb");
        if (!file)
        {
            if (errno == ENOENT)
            {
                readCall.result = k_EResultFileNotFound;
            }
            else
            {
                PrintFileError("opening");
            }
        }
        else
        {
            bool failed = !SeekFile(file, 0, SEEK_END);
            int64 fileSize = failed ? -1 : TellFile(file);
            failed |= fileSize < 0;
            uint32 bytesToRead = 0;
            if (!failed)
            {
                if (static_cast<uint64_t>(offset) < static_cast<uint64_t>(fileSize))
                {
                    uint64_t remaining = static_cast<uint64_t>(fileSize) - offset;
                    bytesToRead = static_cast<uint32>(
                        (std::min)(static_cast<uint64_t>(size), remaining));
                }
                failed = bytesToRead && !SeekFile(file, offset, SEEK_SET);
            }
            if (!failed)
            {
                readCall.data.resize(bytesToRead);
                size_t bytesRead = bytesToRead
                    ? fread(readCall.data.data(), 1, bytesToRead, file)
                    : 0;
                failed = ferror(file) != 0;
                readCall.data.resize(bytesRead);
            }
            failed |= fclose(file) != 0;

            if (failed)
            {
                PrintFileError("reading");
                readCall.data.clear();
            }
            else
            {
                readCall.result = k_EResultOK;
            }
        }
    }

    std::scoped_lock lock{ s_readCallsMutex };
    StoreReadCallLocked(call, std::move(readCall));
    return call;
}

bool IsReadCall(SteamAPICall_t call)
{
    return (call & WriteCallPrefixMask) == ReadCallPrefix;
}

bool GetReadCallResult(SteamAPICall_t call, void *callback, int callbackSize,
    int expectedCallback, bool *failed)
{
    if (!IsReadCall(call)
        || !callback
        || callbackSize != sizeof(RemoteStorageFileReadAsyncComplete_t)
        || expectedCallback != RemoteStorageFileReadAsyncComplete_t::k_iCallback)
    {
        if (failed)
        {
            *failed = true;
        }
        return false;
    }

    std::scoped_lock lock{ s_readCallsMutex };
    auto it = s_readCalls.find(call);
    if (it == s_readCalls.end())
    {
        if (failed)
        {
            *failed = true;
        }
        return false;
    }

    if (failed)
    {
        *failed = false;
    }

    RemoteStorageFileReadAsyncComplete_t result{};
    result.m_hFileReadAsync = call;
    result.m_eResult = it->second.result;
    result.m_nOffset = it->second.offset;
    result.m_cubRead = static_cast<uint32>(it->second.data.size());
    memcpy(callback, &result, sizeof(result));

    if (it->second.result != k_EResultOK)
    {
        EraseReadCallLocked(call);
    }
    return true;
}

bool CompleteReadCall(SteamAPICall_t call, void *buffer, uint32 bufferSize)
{
    if (!IsReadCall(call))
    {
        return false;
    }

    std::scoped_lock lock{ s_readCallsMutex };
    auto it = s_readCalls.find(call);
    if (it == s_readCalls.end() || it->second.result != k_EResultOK)
    {
        return false;
    }

    if (bufferSize < it->second.requestedSize
        || (!buffer && it->second.requestedSize))
    {
        return false;
    }

    if (!it->second.data.empty())
    {
        memcpy(buffer, it->second.data.data(), it->second.data.size());
    }
    EraseReadCallLocked(call);
    return true;
}

} // namespace SavedItemShuffles
