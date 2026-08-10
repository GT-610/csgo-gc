#pragma once

#include <steam/isteamremotestorage.h>

namespace SavedItemShuffles
{

constexpr const char *RemotePath = "cfg/csgo_saved_item_shuffles.txt";
constexpr const char *LocalPath = "csgo_gc/saved_item_shuffles.txt";

bool ShouldUseLocalStorage(uint32_t appId, const char *remotePath);

bool FileWrite(const void *data, uint32_t size);
int32 FileRead(void *data, int32 size);
bool FileExists();
int32 GetFileSize();

SteamAPICall_t MakeWriteCall(bool success);
bool IsWriteCall(SteamAPICall_t call);
bool WriteCallSucceeded(SteamAPICall_t call);
bool GetWriteCallResult(SteamAPICall_t call, void *callback, int callbackSize,
    int expectedCallback, bool *failed);

} // namespace SavedItemShuffles
