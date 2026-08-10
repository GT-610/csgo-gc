#include "stdafx.h"
#include "saved_item_shuffles.h"

#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Platform
{

void Print(const char *, ...)
{
}

} // namespace Platform

namespace
{

namespace fs = std::filesystem;

bool Check(bool condition, const char *expression, int line)
{
    if (!condition)
    {
        fprintf(stderr, "Check failed at line %d: %s\n", line, expression);
    }
    return condition;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void ResetStorage()
{
    std::error_code error;
    fs::remove_all("csgo_gc", error);
    fs::create_directory("csgo_gc", error);
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
    ResetStorage();
    std::array<uint8_t, 4> buffer{};

    return CHECK(!SavedItemShuffles::FileExists())
        && CHECK(SavedItemShuffles::GetFileSize() == 0)
        && CHECK(SavedItemShuffles::FileRead(buffer.data(), buffer.size()) == 0);
}

bool BinaryDataCanBeWrittenReadAndOverwritten()
{
    ResetStorage();
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
    ResetStorage();
    uint8_t byte = 42;

    return CHECK(!SavedItemShuffles::FileWrite(nullptr, 1))
        && CHECK(!SavedItemShuffles::FileWrite(&byte, k_unMaxCloudFileChunkSize + 1))
        && CHECK(SavedItemShuffles::FileRead(nullptr, 1) == 0)
        && CHECK(SavedItemShuffles::FileRead(&byte, 0) == 0)
        && CHECK(SavedItemShuffles::FileRead(&byte, -1) == 0)
        && CHECK(!SavedItemShuffles::FileExists());
}

bool MissingDirectoryMakesWritesFail()
{
    ResetStorage();
    std::error_code error;
    fs::remove_all("csgo_gc", error);
    uint8_t byte = 42;
    bool failed = !SavedItemShuffles::FileWrite(&byte, 1);
    fs::create_directory("csgo_gc", error);
    return CHECK(failed);
}

bool NoTemporaryFilesRemain()
{
    for (const auto &entry : fs::directory_iterator("csgo_gc"))
    {
        std::string name = entry.path().filename().string();
        if (!CHECK(!name.starts_with("saved_item_shuffles.txt.tmp.")))
        {
            return false;
        }
    }
    return true;
}

bool SuccessfulReplacementLeavesNoTemporaryFile()
{
    ResetStorage();
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
    ResetStorage();
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

bool SyntheticCallsAreUniqueAndReportResults()
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

bool SyntheticCallsRejectInvalidResultRequests()
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

} // namespace

int main()
{
    bool success = RoutingIsConservative()
        && MissingFileUsesSteamSemantics()
        && BinaryDataCanBeWrittenReadAndOverwritten()
        && InvalidBuffersAndSizesFail()
        && MissingDirectoryMakesWritesFail()
        && SuccessfulReplacementLeavesNoTemporaryFile()
#ifdef _WIN32
        && FailedReplacementPreservesOldFile()
#endif
        && SyntheticCallsAreUniqueAndReportResults()
        && SyntheticCallsRejectInvalidResultRequests();

    ResetStorage();
    return success ? 0 : 1;
}
