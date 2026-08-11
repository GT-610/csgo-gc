#include "stdafx.h"
#include "saved_item_shuffles.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Platform
{

void Print(const char *, ...)
{
}

} // namespace Platform

namespace
{

constexpr const char *TemporaryFilePrefix = "saved_item_shuffles.txt.tmp.";
constexpr const char *LocalFileName = "saved_item_shuffles.txt";

bool Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "Check failed at line %d: %s\n", line, expression);
    }
    return condition;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

bool IsStorageFile(const char *name)
{
    return !strcmp(name, LocalFileName)
        || !strncmp(name, TemporaryFilePrefix, strlen(TemporaryFilePrefix));
}

bool RemoveStoragePath(bool removeDirectory)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(SavedItemShuffles::DirectoryPath);
    if (attributes == INVALID_FILE_ATTRIBUTES)
    {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }

    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
    {
        return DeleteFileA(SavedItemShuffles::DirectoryPath) != 0;
    }

    std::string pattern = SavedItemShuffles::DirectoryPath;
    pattern.append("\\*");
    WIN32_FIND_DATAA entry{};
    HANDLE search = FindFirstFileA(pattern.c_str(), &entry);
    bool succeeded = true;
    if (search != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!strcmp(entry.cFileName, ".") || !strcmp(entry.cFileName, ".."))
            {
                continue;
            }

            if (IsStorageFile(entry.cFileName))
            {
                std::string path = SavedItemShuffles::DirectoryPath;
                path.push_back('\\');
                path.append(entry.cFileName);
                if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                    || !DeleteFileA(path.c_str()))
                {
                    succeeded = false;
                }
            }
        } while (FindNextFileA(search, &entry));
        FindClose(search);
        if (!succeeded)
        {
            return false;
        }
    }
    else if (GetLastError() != ERROR_FILE_NOT_FOUND)
    {
        return false;
    }

    return succeeded && (!removeDirectory
        || RemoveDirectoryA(SavedItemShuffles::DirectoryPath) != 0);
#else
    struct stat status{};
    if (lstat(SavedItemShuffles::DirectoryPath, &status) != 0)
    {
        return errno == ENOENT;
    }

    if (!S_ISDIR(status.st_mode))
    {
        return unlink(SavedItemShuffles::DirectoryPath) == 0;
    }

    DIR *directory = opendir(SavedItemShuffles::DirectoryPath);
    if (!directory)
    {
        return false;
    }

    bool succeeded = true;
    while (dirent *entry = readdir(directory))
    {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
        {
            continue;
        }

        if (IsStorageFile(entry->d_name))
        {
            std::string path = SavedItemShuffles::DirectoryPath;
            path.push_back('/');
            path.append(entry->d_name);
            if (unlink(path.c_str()) != 0)
            {
                succeeded = false;
            }
        }
    }
    succeeded &= closedir(directory) == 0;
    return succeeded && (!removeDirectory
        || rmdir(SavedItemShuffles::DirectoryPath) == 0);
#endif
}

bool EnsureStorageDirectory()
{
#ifdef _WIN32
    if (CreateDirectoryA(SavedItemShuffles::DirectoryPath, nullptr))
    {
        return true;
    }
    DWORD error = GetLastError();
    DWORD attributes = GetFileAttributesA(SavedItemShuffles::DirectoryPath);
    return error == ERROR_ALREADY_EXISTS
        && attributes != INVALID_FILE_ATTRIBUTES
        && (attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    if (mkdir(SavedItemShuffles::DirectoryPath, 0755) == 0)
    {
        return true;
    }
    int error = errno;
    struct stat status{};
    return error == EEXIST
        && stat(SavedItemShuffles::DirectoryPath, &status) == 0
        && S_ISDIR(status.st_mode);
#endif
}

bool ResetStorage()
{
    return RemoveStoragePath(false) && EnsureStorageDirectory();
}

std::string StoragePath(const char *name)
{
    std::string path = SavedItemShuffles::DirectoryPath;
#ifdef _WIN32
    path.push_back('\\');
#else
    path.push_back('/');
#endif
    path.append(name);
    return path;
}

bool ResetStoragePreservesUnrelatedFiles()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    const uint8_t data = 42;
    if (!CHECK(SavedItemShuffles::FileWrite(&data, 1)))
    {
        return false;
    }

    std::string unrelatedPath = StoragePath("saved_item_shuffles_test_unrelated.txt");
    FILE *unrelatedFile = fopen(unrelatedPath.c_str(), "wb");
    if (!CHECK(unrelatedFile != nullptr))
    {
        return false;
    }
    bool created = fwrite(&data, 1, 1, unrelatedFile) == 1;
    created &= fclose(unrelatedFile) == 0;

    bool reset = created && ResetStorage();
    FILE *preservedFile = reset ? fopen(unrelatedPath.c_str(), "rb") : nullptr;
    bool preserved = preservedFile != nullptr;
    if (preservedFile)
    {
        preserved &= fclose(preservedFile) == 0;
    }
    bool removed = remove(unrelatedPath.c_str()) == 0;

    return CHECK(created)
        && CHECK(reset)
        && CHECK(!SavedItemShuffles::FileExists())
        && CHECK(preserved)
        && CHECK(removed);
}

bool RoutingIsConservative()
{
    using SavedItemShuffles::ShouldUseLocalStorage;

    return CHECK(!ShouldUseLocalStorage(730, SavedItemShuffles::RemotePath))
        && CHECK(ShouldUseLocalStorage(4465480, SavedItemShuffles::RemotePath))
        && CHECK(ShouldUseLocalStorage(1, "CFG/CSGO_SAVED_ITEM_SHUFFLES.TXT"))
        && CHECK(!ShouldUseLocalStorage(4465480, "cfg/other.txt"))
        && CHECK(!ShouldUseLocalStorage(4465480, "cfg/csgo_saved_item_shuffles.txt.bak"))
        && CHECK(!ShouldUseLocalStorage(4465480, nullptr));
}

bool MissingFileUsesSteamSemantics()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }
    std::array<uint8_t, 4> buffer{};

    return CHECK(!SavedItemShuffles::FileExists())
        && CHECK(!SavedItemShuffles::FilePersisted())
        && CHECK(!SavedItemShuffles::FileForget())
        && CHECK(!SavedItemShuffles::FileDelete())
        && CHECK(SavedItemShuffles::GetFileSize() == 0)
        && CHECK(SavedItemShuffles::GetFileTimestamp() == 0)
        && CHECK(SavedItemShuffles::FileRead(buffer.data(), buffer.size()) == 0);
}

bool BinaryDataCanBeWrittenReadAndOverwritten()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }
    const std::array<uint8_t, 7> first{ 0, 1, 2, 0xff, 0, 4, 5 };
    const std::array<uint8_t, 4> second{ 9, 0, 8, 7 };
    std::array<uint8_t, 8> buffer{};

    if (!CHECK(SavedItemShuffles::FileWrite(first.data(), first.size()))
        || !CHECK(SavedItemShuffles::FileExists())
        || !CHECK(SavedItemShuffles::GetFileSize() == static_cast<int32>(first.size()))
        || !CHECK(SavedItemShuffles::FileRead(buffer.data(), buffer.size()) == static_cast<int32>(first.size()))
        || !CHECK(std::equal(first.begin(), first.end(), buffer.begin())))
    {
        return false;
    }

    buffer.fill(0);
    return CHECK(SavedItemShuffles::FileWrite(second.data(), second.size()))
        && CHECK(SavedItemShuffles::GetFileSize() == static_cast<int32>(second.size()))
        && CHECK(SavedItemShuffles::FileRead(buffer.data(), buffer.size()) == static_cast<int32>(second.size()))
        && CHECK(std::equal(second.begin(), second.end(), buffer.begin()));
}

bool InvalidBuffersAndSizesFail()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }
    uint8_t byte = 42;

    return CHECK(!SavedItemShuffles::FileWrite(nullptr, 1))
        && CHECK(!SavedItemShuffles::FileWrite(&byte, k_unMaxCloudFileChunkSize + 1))
        && CHECK(SavedItemShuffles::FileRead(nullptr, 1) == 0)
        && CHECK(SavedItemShuffles::FileRead(&byte, 0) == 0)
        && CHECK(SavedItemShuffles::FileRead(&byte, -1) == 0)
        && CHECK(!SavedItemShuffles::FileExists());
}

bool MissingDirectoryIsCreatedOnWrite()
{
    if (!CHECK(ResetStorage()) || !CHECK(RemoveStoragePath(true)))
    {
        return false;
    }

    uint8_t byte = 42;
    uint8_t result = 0;
    return CHECK(SavedItemShuffles::FileWrite(&byte, 1))
        && CHECK(SavedItemShuffles::FileExists())
        && CHECK(SavedItemShuffles::FileRead(&result, 1) == 1)
        && CHECK(result == byte);
}

bool DirectoryCreationFailureStopsWrite()
{
    if (!CHECK(ResetStorage()) || !CHECK(RemoveStoragePath(true)))
    {
        return false;
    }

    FILE *blockingFile = fopen(SavedItemShuffles::DirectoryPath, "wb");
    if (!CHECK(blockingFile != nullptr))
    {
        return false;
    }
    bool closed = fclose(blockingFile) == 0;

    uint8_t byte = 42;
    return CHECK(closed)
        && CHECK(!SavedItemShuffles::FileWrite(&byte, 1));
}

bool NoTemporaryFilesRemain()
{
#ifdef _WIN32
    std::string pattern = SavedItemShuffles::DirectoryPath;
    pattern.append("\\*");
    WIN32_FIND_DATAA entry{};
    HANDLE search = FindFirstFileA(pattern.c_str(), &entry);
    if (!CHECK(search != INVALID_HANDLE_VALUE))
    {
        return false;
    }

    bool succeeded = true;
    do
    {
        if (!strncmp(entry.cFileName, TemporaryFilePrefix,
                strlen(TemporaryFilePrefix)))
        {
            succeeded = CHECK(false);
        }
    } while (FindNextFileA(search, &entry));
    FindClose(search);
    return succeeded;
#else
    DIR *directory = opendir(SavedItemShuffles::DirectoryPath);
    if (!CHECK(directory != nullptr))
    {
        return false;
    }

    bool succeeded = true;
    while (dirent *entry = readdir(directory))
    {
        if (!strncmp(entry->d_name, TemporaryFilePrefix,
                strlen(TemporaryFilePrefix)))
        {
            succeeded = CHECK(false);
        }
    }
    succeeded &= closedir(directory) == 0;
    return succeeded;
#endif
}

bool SuccessfulReplacementLeavesNoTemporaryFile()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }
    const std::array<uint8_t, 3> data{ 1, 2, 3 };
    if (!CHECK(SavedItemShuffles::FileWrite(data.data(), data.size())))
    {
        return false;
    }

    return NoTemporaryFilesRemain();
}

#ifdef _WIN32
bool FailedReplacementPreservesOldFile()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }
    const std::array<uint8_t, 3> oldData{ 1, 2, 3 };
    const std::array<uint8_t, 3> newData{ 4, 5, 6 };
    if (!CHECK(SavedItemShuffles::FileWrite(oldData.data(), oldData.size())))
    {
        return false;
    }

    HANDLE file = CreateFileA(SavedItemShuffles::LocalPath, GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (!CHECK(file != INVALID_HANDLE_VALUE))
    {
        return false;
    }

    bool writeFailed = !SavedItemShuffles::FileWrite(newData.data(), newData.size());
    CloseHandle(file);

    std::array<uint8_t, 3> buffer{};
    return CHECK(writeFailed)
        && CHECK(SavedItemShuffles::FileRead(buffer.data(), buffer.size()) == static_cast<int32>(buffer.size()))
        && CHECK(buffer == oldData)
        && CHECK(NoTemporaryFilesRemain());
}
#endif

bool MetadataForgetAndDeleteUseLocalFile()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    const std::array<uint8_t, 3> data{ 1, 2, 3 };
    if (!CHECK(SavedItemShuffles::FileWrite(data.data(), data.size())))
    {
        return false;
    }

    return CHECK(SavedItemShuffles::FilePersisted())
        && CHECK(SavedItemShuffles::GetFileTimestamp() > 0)
        && CHECK(SavedItemShuffles::FileForget())
        && CHECK(SavedItemShuffles::FileExists())
        && CHECK(SavedItemShuffles::FileDelete())
        && CHECK(!SavedItemShuffles::FileExists())
        && CHECK(!SavedItemShuffles::FilePersisted())
        && CHECK(SavedItemShuffles::GetFileTimestamp() == 0)
        && CHECK(!SavedItemShuffles::FileDelete());
}

bool SyntheticWriteCallsAreUniqueAndReportResults()
{
    SteamAPICall_t successCall = SavedItemShuffles::MakeWriteCall(true);
    SteamAPICall_t failureCall = SavedItemShuffles::MakeWriteCall(false);
    SteamAPICall_t anotherCall = SavedItemShuffles::MakeWriteCall(true);

    if (!CHECK(successCall != failureCall)
        || !CHECK(successCall != anotherCall)
        || !CHECK(failureCall != anotherCall)
        || !CHECK(SavedItemShuffles::IsWriteCall(successCall))
        || !CHECK(SavedItemShuffles::IsWriteCall(failureCall))
        || !CHECK(!SavedItemShuffles::IsWriteCall(0x6666666666666666ULL))
        || !CHECK(SavedItemShuffles::WriteCallSucceeded(successCall))
        || !CHECK(!SavedItemShuffles::WriteCallSucceeded(failureCall)))
    {
        return false;
    }

    RemoteStorageFileWriteAsyncComplete_t result{};
    bool failed = true;
    if (!CHECK(SavedItemShuffles::GetWriteCallResult(successCall, &result,
            sizeof(result), result.k_iCallback, &failed))
        || !CHECK(!failed)
        || !CHECK(result.m_eResult == k_EResultOK))
    {
        return false;
    }

    failed = true;
    result = {};
    return CHECK(SavedItemShuffles::GetWriteCallResult(failureCall, &result,
               sizeof(result), result.k_iCallback, &failed))
        && CHECK(!failed)
        && CHECK(result.m_eResult == k_EResultFail);
}

bool SyntheticWriteCallsRejectInvalidResultRequests()
{
    SteamAPICall_t call = SavedItemShuffles::MakeWriteCall(true);
    RemoteStorageFileWriteAsyncComplete_t result{};
    bool failed = false;

    if (!CHECK(!SavedItemShuffles::GetWriteCallResult(call, nullptr,
            sizeof(result), result.k_iCallback, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    if (!CHECK(!SavedItemShuffles::GetWriteCallResult(call, &result,
            sizeof(result) - 1, result.k_iCallback, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    if (!CHECK(!SavedItemShuffles::GetWriteCallResult(call, &result,
            sizeof(result), result.k_iCallback + 1, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    return CHECK(!SavedItemShuffles::GetWriteCallResult(123, &result,
               sizeof(result), result.k_iCallback, &failed))
        && CHECK(failed);
}

bool SyntheticReadCallsReturnLocalData()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    const std::array<uint8_t, 6> data{ 0, 1, 2, 3, 4, 5 };
    if (!CHECK(SavedItemShuffles::FileWrite(data.data(), data.size())))
    {
        return false;
    }

    SteamAPICall_t call = SavedItemShuffles::MakeReadCall(2, 3);
    SteamAPICall_t anotherCall = SavedItemShuffles::MakeReadCall(0, 1);
    SteamAPICall_t shortReadCall = SavedItemShuffles::MakeReadCall(4, 4);
    if (!CHECK(call != anotherCall)
        || !CHECK(call != shortReadCall)
        || !CHECK(anotherCall != shortReadCall)
        || !CHECK(SavedItemShuffles::IsReadCall(call))
        || !CHECK(SavedItemShuffles::IsReadCall(anotherCall))
        || !CHECK(!SavedItemShuffles::IsWriteCall(call))
        || !CHECK(!SavedItemShuffles::IsReadCall(0x6666666666666666ULL)))
    {
        return false;
    }

    RemoteStorageFileReadAsyncComplete_t result{};
    bool failed = true;
    if (!CHECK(SavedItemShuffles::GetReadCallResult(call, &result,
            sizeof(result), result.k_iCallback, &failed))
        || !CHECK(!failed)
        || !CHECK(result.m_hFileReadAsync == call)
        || !CHECK(result.m_eResult == k_EResultOK)
        || !CHECK(result.m_nOffset == 2)
        || !CHECK(result.m_cubRead == 3))
    {
        return false;
    }

    std::array<uint8_t, 2> smallBuffer{};
    if (!CHECK(!SavedItemShuffles::CompleteReadCall(call,
            smallBuffer.data(), smallBuffer.size())))
    {
        return false;
    }

    std::array<uint8_t, 3> buffer{};
    std::array<uint8_t, 2> shortBuffer{};
    std::array<uint8_t, 4> requestedBuffer{};
    uint8_t firstByte = 0xff;
    return CHECK(SavedItemShuffles::CompleteReadCall(call,
               buffer.data(), buffer.size()))
        && CHECK((buffer == std::array<uint8_t, 3>{ 2, 3, 4 }))
        && CHECK(!SavedItemShuffles::CompleteReadCall(call,
            buffer.data(), buffer.size()))
        && CHECK(SavedItemShuffles::CompleteReadCall(anotherCall, &firstByte, 1))
        && CHECK(firstByte == 0)
        && CHECK(!SavedItemShuffles::CompleteReadCall(shortReadCall,
            shortBuffer.data(), shortBuffer.size()))
        && CHECK(SavedItemShuffles::CompleteReadCall(shortReadCall,
            requestedBuffer.data(), requestedBuffer.size()))
        && CHECK(requestedBuffer[0] == 4)
        && CHECK(requestedBuffer[1] == 5);
}

bool SyntheticReadCallsReportFailures()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    SteamAPICall_t missingCall = SavedItemShuffles::MakeReadCall(4, 8);
    RemoteStorageFileReadAsyncComplete_t result{};
    bool failed = true;
    if (!CHECK(SavedItemShuffles::GetReadCallResult(missingCall, &result,
            sizeof(result), result.k_iCallback, &failed))
        || !CHECK(!failed)
        || !CHECK(result.m_hFileReadAsync == missingCall)
        || !CHECK(result.m_eResult == k_EResultFileNotFound)
        || !CHECK(result.m_nOffset == 4)
        || !CHECK(result.m_cubRead == 0)
        || !CHECK(!SavedItemShuffles::CompleteReadCall(missingCall, nullptr, 0)))
    {
        return false;
    }

    SteamAPICall_t invalidCall = SavedItemShuffles::MakeReadCall(
        0, k_unMaxCloudFileChunkSize + 1);
    result = {};
    failed = true;
    return CHECK(SavedItemShuffles::GetReadCallResult(invalidCall, &result,
               sizeof(result), result.k_iCallback, &failed))
        && CHECK(!failed)
        && CHECK(result.m_eResult == k_EResultInvalidParam)
        && CHECK(result.m_cubRead == 0);
}

bool SyntheticReadCallsRejectInvalidResultRequests()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    const uint8_t data = 42;
    if (!CHECK(SavedItemShuffles::FileWrite(&data, 1)))
    {
        return false;
    }

    SteamAPICall_t call = SavedItemShuffles::MakeReadCall(0, 1);
    RemoteStorageFileReadAsyncComplete_t result{};
    bool failed = false;
    if (!CHECK(!SavedItemShuffles::GetReadCallResult(call, nullptr,
            sizeof(result), result.k_iCallback, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    if (!CHECK(!SavedItemShuffles::GetReadCallResult(call, &result,
            sizeof(result) - 1, result.k_iCallback, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    if (!CHECK(!SavedItemShuffles::GetReadCallResult(call, &result,
            sizeof(result), result.k_iCallback + 1, &failed))
        || !CHECK(failed))
    {
        return false;
    }

    failed = false;
    uint8_t buffer = 0;
    return CHECK(!SavedItemShuffles::GetReadCallResult(123, &result,
               sizeof(result), result.k_iCallback, &failed))
        && CHECK(failed)
        && CHECK(SavedItemShuffles::CompleteReadCall(call, &buffer, 1))
        && CHECK(buffer == data);
}

bool SyntheticReadCallsEvictOldestPendingCall()
{
    if (!CHECK(ResetStorage()))
    {
        return false;
    }

    const uint8_t data = 42;
    if (!CHECK(SavedItemShuffles::FileWrite(&data, 1)))
    {
        return false;
    }

    std::array<SteamAPICall_t, 17> calls{};
    for (SteamAPICall_t &call : calls)
    {
        call = SavedItemShuffles::MakeReadCall(0, 0);
    }

    RemoteStorageFileReadAsyncComplete_t result{};
    bool failed = false;
    bool success = CHECK(!SavedItemShuffles::GetReadCallResult(calls.front(),
        &result, sizeof(result), result.k_iCallback, &failed))
        && CHECK(failed);

    for (size_t index = 1; index < calls.size(); ++index)
    {
        result = {};
        failed = true;
        success &= CHECK(SavedItemShuffles::GetReadCallResult(calls[index],
            &result, sizeof(result), result.k_iCallback, &failed));
        success &= CHECK(!failed);
        success &= CHECK(result.m_hFileReadAsync == calls[index]);
        success &= CHECK(result.m_eResult == k_EResultOK);
        success &= CHECK(result.m_cubRead == 0);
        success &= CHECK(SavedItemShuffles::CompleteReadCall(
            calls[index], nullptr, 0));
    }
    return success;
}

} // namespace

int main()
{
    bool success = true;
    success &= RoutingIsConservative();
    success &= ResetStoragePreservesUnrelatedFiles();
    success &= MissingFileUsesSteamSemantics();
    success &= BinaryDataCanBeWrittenReadAndOverwritten();
    success &= InvalidBuffersAndSizesFail();
    success &= MissingDirectoryIsCreatedOnWrite();
    success &= DirectoryCreationFailureStopsWrite();
    success &= SuccessfulReplacementLeavesNoTemporaryFile();
#ifdef _WIN32
    success &= FailedReplacementPreservesOldFile();
#endif
    success &= MetadataForgetAndDeleteUseLocalFile();
    success &= SyntheticWriteCallsAreUniqueAndReportResults();
    success &= SyntheticWriteCallsRejectInvalidResultRequests();
    success &= SyntheticReadCallsReturnLocalData();
    success &= SyntheticReadCallsReportFailures();
    success &= SyntheticReadCallsRejectInvalidResultRequests();
    success &= SyntheticReadCallsEvictOldestPendingCall();

    success &= ResetStorage();
    return success ? 0 : 1;
}
