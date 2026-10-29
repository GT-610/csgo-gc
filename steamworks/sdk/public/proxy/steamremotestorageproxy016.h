// Automatically generated interface forwarding glue. Do not modify by hand.
#ifndef STEAMREMOTESTORAGEPROXY016_H
#define STEAMREMOTESTORAGEPROXY016_H

#include "steamproxy.h"
#include <steam_old/isteamremotestorage016.h>

class SteamRemoteStorageProxy016 final : public ISteamRemoteStorage016
{
    ISteamRemoteStorage016 *m_original;
    SteamRemoteStorageProxy *m_proxy;

public:
    SteamRemoteStorageProxy016(ISteamRemoteStorage016 *original, SteamRemoteStorageProxy *proxy)
        : m_original{ original }
        , m_proxy{ proxy }
    {
    }

#define STEAM_REMOTE_STORAGE_PROXY_METHOD(returnType, name, parameters, ...) \
    returnType name parameters override \
    { \
        return [&](auto p) -> returnType \
        { \
            auto o = MakeOrig<&ISteamRemoteStorage016::name>(m_original); \
            if constexpr (requires { p->name(o __VA_OPT__(,) __VA_ARGS__); }) \
                return p->name(o __VA_OPT__(,) __VA_ARGS__); \
            else \
                return o(__VA_ARGS__); \
        }(m_proxy); \
    }

    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileWrite, (const char *pchFile, const void *pvData, int32 cubData), pchFile, pvData, cubData)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, FileRead, (const char *pchFile, void *pvData, int32 cubDataToRead), pchFile, pvData, cubDataToRead)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, FileWriteAsync, (const char *pchFile, const void *pvData, uint32 cubData), pchFile, pvData, cubData)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, FileReadAsync, (const char *pchFile, uint32 nOffset, uint32 cubToRead), pchFile, nOffset, cubToRead)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileReadAsyncComplete, (SteamAPICall_t hReadCall, void *pvBuffer, uint32 cubToRead), hReadCall, pvBuffer, cubToRead)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileForget, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileDelete, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, FileShare, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, SetSyncPlatforms, (const char *pchFile, ERemoteStoragePlatform eRemoteStoragePlatform), pchFile, eRemoteStoragePlatform)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(UGCFileWriteStreamHandle_t, FileWriteStreamOpen, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileWriteStreamWriteChunk, (UGCFileWriteStreamHandle_t writeHandle, const void *pvData, int32 cubData), writeHandle, pvData, cubData)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileWriteStreamClose, (UGCFileWriteStreamHandle_t writeHandle), writeHandle)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileWriteStreamCancel, (UGCFileWriteStreamHandle_t writeHandle), writeHandle)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FileExists, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, FilePersisted, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, GetFileSize, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int64, GetFileTimestamp, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(ERemoteStoragePlatform, GetSyncPlatforms, (const char *pchFile), pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, GetFileCount, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(const char *, GetFileNameAndSize, (int iFile, int32 *pnFileSizeInBytes), iFile, pnFileSizeInBytes)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, GetQuota, (uint64 *pnTotalBytes, uint64 *puAvailableBytes), pnTotalBytes, puAvailableBytes)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, IsCloudEnabledForAccount, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, IsCloudEnabledForApp, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(void, SetCloudEnabledForApp, (bool bEnabled), bEnabled)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, UGCDownload, (UGCHandle_t hContent, uint32 unPriority), hContent, unPriority)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, GetUGCDownloadProgress, (UGCHandle_t hContent, int32 *pnBytesDownloaded, int32 *pnBytesExpected), hContent, pnBytesDownloaded, pnBytesExpected)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, GetUGCDetails, (UGCHandle_t hContent, AppId_t *pnAppID, char **ppchName, int32 *pnFileSizeInBytes, CSteamID *pSteamIDOwner), hContent, pnAppID, ppchName, pnFileSizeInBytes, pSteamIDOwner)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, UGCRead, (UGCHandle_t hContent, void *pvData, int32 cubDataToRead, uint32 cOffset, EUGCReadAction eAction), hContent, pvData, cubDataToRead, cOffset, eAction)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, GetCachedUGCCount, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(UGCHandle_t, GetCachedUGCHandle, (int32 iCachedContent), iCachedContent)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, PublishWorkshopFile, (const char *pchFile, const char *pchPreviewFile, AppId_t nConsumerAppId, const char *pchTitle, const char *pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t *pTags, EWorkshopFileType eWorkshopFileType), pchFile, pchPreviewFile, nConsumerAppId, pchTitle, pchDescription, eVisibility, pTags, eWorkshopFileType)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(PublishedFileUpdateHandle_t, CreatePublishedFileUpdateRequest, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileFile, (PublishedFileUpdateHandle_t updateHandle, const char *pchFile), updateHandle, pchFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFilePreviewFile, (PublishedFileUpdateHandle_t updateHandle, const char *pchPreviewFile), updateHandle, pchPreviewFile)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileTitle, (PublishedFileUpdateHandle_t updateHandle, const char *pchTitle), updateHandle, pchTitle)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileDescription, (PublishedFileUpdateHandle_t updateHandle, const char *pchDescription), updateHandle, pchDescription)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileVisibility, (PublishedFileUpdateHandle_t updateHandle, ERemoteStoragePublishedFileVisibility eVisibility), updateHandle, eVisibility)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileTags, (PublishedFileUpdateHandle_t updateHandle, SteamParamStringArray_t *pTags), updateHandle, pTags)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, CommitPublishedFileUpdate, (PublishedFileUpdateHandle_t updateHandle), updateHandle)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, GetPublishedFileDetails, (PublishedFileId_t unPublishedFileId, uint32 unMaxSecondsOld), unPublishedFileId, unMaxSecondsOld)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, DeletePublishedFile, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, EnumerateUserPublishedFiles, (uint32 unStartIndex), unStartIndex)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, SubscribePublishedFile, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, EnumerateUserSubscribedFiles, (uint32 unStartIndex), unStartIndex)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, UnsubscribePublishedFile, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, UpdatePublishedFileSetChangeDescription, (PublishedFileUpdateHandle_t updateHandle, const char *pchChangeDescription), updateHandle, pchChangeDescription)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, GetPublishedItemVoteDetails, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, UpdateUserPublishedItemVote, (PublishedFileId_t unPublishedFileId, bool bVoteUp), unPublishedFileId, bVoteUp)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, GetUserPublishedItemVoteDetails, (PublishedFileId_t unPublishedFileId), unPublishedFileId)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, EnumerateUserSharedWorkshopFiles, (CSteamID steamId, uint32 unStartIndex, SteamParamStringArray_t *pRequiredTags, SteamParamStringArray_t *pExcludedTags), steamId, unStartIndex, pRequiredTags, pExcludedTags)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, PublishVideo, (EWorkshopVideoProvider eVideoProvider, const char *pchVideoAccount, const char *pchVideoIdentifier, const char *pchPreviewFile, AppId_t nConsumerAppId, const char *pchTitle, const char *pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t *pTags), eVideoProvider, pchVideoAccount, pchVideoIdentifier, pchPreviewFile, nConsumerAppId, pchTitle, pchDescription, eVisibility, pTags)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, SetUserPublishedFileAction, (PublishedFileId_t unPublishedFileId, EWorkshopFileAction eAction), unPublishedFileId, eAction)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, EnumeratePublishedFilesByUserAction, (EWorkshopFileAction eAction, uint32 unStartIndex), eAction, unStartIndex)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, EnumeratePublishedWorkshopFiles, (EWorkshopEnumerationType eEnumerationType, uint32 unStartIndex, uint32 unCount, uint32 unDays, SteamParamStringArray_t *pTags, SteamParamStringArray_t *pUserTags), eEnumerationType, unStartIndex, unCount, unDays, pTags, pUserTags)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(SteamAPICall_t, UGCDownloadToLocation, (UGCHandle_t hContent, const char *pchLocation, uint32 unPriority), hContent, pchLocation, unPriority)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(int32, GetLocalFileChangeCount, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(const char *, GetLocalFileChange, (int iFile, ERemoteStorageLocalFileChange *pEChangeType, ERemoteStorageFilePathType *pEFilePathType), iFile, pEChangeType, pEFilePathType)
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, BeginFileWriteBatch, ())
    STEAM_REMOTE_STORAGE_PROXY_METHOD(bool, EndFileWriteBatch, ())

#undef STEAM_REMOTE_STORAGE_PROXY_METHOD
};

#endif // STEAMREMOTESTORAGEPROXY016_H
