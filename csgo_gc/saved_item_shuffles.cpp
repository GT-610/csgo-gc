#include "stdafx.h"
#include "saved_item_shuffles.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace SavedItemShuffles
{

namespace
{

constexpr uint32_t OriginalAppId = 730;
constexpr uint64_t WriteCallPrefix = 0x4353474f00000000ULL;
constexpr uint64_t WriteCallPrefixMask = 0xffffffff00000000ULL;
constexpr uint64_t WriteCallSuccessMask = 1;

std::atomic<uint32_t> s_temporaryFileCounter;
std::atomic<uint32_t> s_writeCallCounter{ 1 };

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

} // namespace SavedItemShuffles
