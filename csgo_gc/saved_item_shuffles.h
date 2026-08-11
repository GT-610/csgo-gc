#pragma once

#include <steam/isteamremotestorage.h>

namespace SavedItemShuffles
{

constexpr const char *RemotePath = "cfg/csgo_saved_item_shuffles.txt";
constexpr const char *DirectoryPath = "csgo_gc";
constexpr const char *LocalPath = "csgo_gc/saved_item_shuffles.txt";

bool ShouldUseLocalStorage(uint32_t appId, const char *remotePath);

bool FileWrite(const void *data, uint32_t size);
int32 FileRead(void *data, int32 size);
bool FileForget();
bool FileDelete();
bool FileExists();
bool FilePersisted();
int32 GetFileSize();
int64 GetFileTimestamp();

SteamAPICall_t MakeWriteCall(bool success);
bool IsWriteCall(SteamAPICall_t call);
bool WriteCallSucceeded(SteamAPICall_t call);
bool GetWriteCallResult(SteamAPICall_t call, void *callback, int callbackSize,
    int expectedCallback, bool *failed);

SteamAPICall_t MakeReadCall(uint32 offset, uint32 size);
bool IsReadCall(SteamAPICall_t call);
bool GetReadCallResult(SteamAPICall_t call, void *callback, int callbackSize,
    int expectedCallback, bool *failed);
bool CompleteReadCall(SteamAPICall_t call, void *buffer, uint32 bufferSize);

} // namespace SavedItemShuffles
