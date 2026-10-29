// Extracted from steamworks_sdk_142
#ifndef ISTEAMREMOTESTORAGE014_H
#define ISTEAMREMOTESTORAGE014_H

class ISteamRemoteStorage014
{
public:
    virtual bool FileWrite(const char *pchFile, const void *pvData, int32 cubData) = 0;
    virtual int32 FileRead(const char *pchFile, void *pvData, int32 cubDataToRead) = 0;
    virtual SteamAPICall_t FileWriteAsync(const char *pchFile, const void *pvData, uint32 cubData) = 0;
    virtual SteamAPICall_t FileReadAsync(const char *pchFile, uint32 nOffset, uint32 cubToRead) = 0;
    virtual bool FileReadAsyncComplete(SteamAPICall_t hReadCall, void *pvBuffer, uint32 cubToRead) = 0;
    virtual bool FileForget(const char *pchFile) = 0;
    virtual bool FileDelete(const char *pchFile) = 0;
    virtual SteamAPICall_t FileShare(const char *pchFile) = 0;
    virtual bool SetSyncPlatforms(const char *pchFile, ERemoteStoragePlatform eRemoteStoragePlatform) = 0;
    virtual UGCFileWriteStreamHandle_t FileWriteStreamOpen(const char *pchFile) = 0;
    virtual bool FileWriteStreamWriteChunk(UGCFileWriteStreamHandle_t writeHandle, const void *pvData, int32 cubData) = 0;
    virtual bool FileWriteStreamClose(UGCFileWriteStreamHandle_t writeHandle) = 0;
    virtual bool FileWriteStreamCancel(UGCFileWriteStreamHandle_t writeHandle) = 0;
    virtual bool FileExists(const char *pchFile) = 0;
    virtual bool FilePersisted(const char *pchFile) = 0;
    virtual int32 GetFileSize(const char *pchFile) = 0;
    virtual int64 GetFileTimestamp(const char *pchFile) = 0;
    virtual ERemoteStoragePlatform GetSyncPlatforms(const char *pchFile) = 0;
    virtual int32 GetFileCount() = 0;
    virtual const char *GetFileNameAndSize(int iFile, int32 *pnFileSizeInBytes) = 0;
    virtual bool GetQuota(uint64 *pnTotalBytes, uint64 *puAvailableBytes) = 0;
    virtual bool IsCloudEnabledForAccount() = 0;
    virtual bool IsCloudEnabledForApp() = 0;
    virtual void SetCloudEnabledForApp(bool bEnabled) = 0;
    virtual SteamAPICall_t UGCDownload(UGCHandle_t hContent, uint32 unPriority) = 0;
    virtual bool GetUGCDownloadProgress(UGCHandle_t hContent, int32 *pnBytesDownloaded, int32 *pnBytesExpected) = 0;
    virtual bool GetUGCDetails(UGCHandle_t hContent, AppId_t *pnAppID, STEAM_OUT_STRING() char **ppchName, int32 *pnFileSizeInBytes, STEAM_OUT_STRUCT() CSteamID *pSteamIDOwner) = 0;
    virtual int32 UGCRead(UGCHandle_t hContent, void *pvData, int32 cubDataToRead, uint32 cOffset, EUGCReadAction eAction) = 0;
    virtual int32 GetCachedUGCCount() = 0;
    virtual UGCHandle_t GetCachedUGCHandle(int32 iCachedContent) = 0;
    virtual SteamAPICall_t PublishWorkshopFile(const char *pchFile, const char *pchPreviewFile, AppId_t nConsumerAppId, const char *pchTitle, const char *pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t *pTags, EWorkshopFileType eWorkshopFileType) = 0;
    virtual PublishedFileUpdateHandle_t CreatePublishedFileUpdateRequest(PublishedFileId_t unPublishedFileId) = 0;
    virtual bool UpdatePublishedFileFile(PublishedFileUpdateHandle_t updateHandle, const char *pchFile) = 0;
    virtual bool UpdatePublishedFilePreviewFile(PublishedFileUpdateHandle_t updateHandle, const char *pchPreviewFile) = 0;
    virtual bool UpdatePublishedFileTitle(PublishedFileUpdateHandle_t updateHandle, const char *pchTitle) = 0;
    virtual bool UpdatePublishedFileDescription(PublishedFileUpdateHandle_t updateHandle, const char *pchDescription) = 0;
    virtual bool UpdatePublishedFileVisibility(PublishedFileUpdateHandle_t updateHandle, ERemoteStoragePublishedFileVisibility eVisibility) = 0;
    virtual bool UpdatePublishedFileTags(PublishedFileUpdateHandle_t updateHandle, SteamParamStringArray_t *pTags) = 0;
    virtual SteamAPICall_t CommitPublishedFileUpdate(PublishedFileUpdateHandle_t updateHandle) = 0;
    virtual SteamAPICall_t GetPublishedFileDetails(PublishedFileId_t unPublishedFileId, uint32 unMaxSecondsOld) = 0;
    virtual SteamAPICall_t DeletePublishedFile(PublishedFileId_t unPublishedFileId) = 0;
    virtual SteamAPICall_t EnumerateUserPublishedFiles(uint32 unStartIndex) = 0;
    virtual SteamAPICall_t SubscribePublishedFile(PublishedFileId_t unPublishedFileId) = 0;
    virtual SteamAPICall_t EnumerateUserSubscribedFiles(uint32 unStartIndex) = 0;
    virtual SteamAPICall_t UnsubscribePublishedFile(PublishedFileId_t unPublishedFileId) = 0;
    virtual bool UpdatePublishedFileSetChangeDescription(PublishedFileUpdateHandle_t updateHandle, const char *pchChangeDescription) = 0;
    virtual SteamAPICall_t GetPublishedItemVoteDetails(PublishedFileId_t unPublishedFileId) = 0;
    virtual SteamAPICall_t UpdateUserPublishedItemVote(PublishedFileId_t unPublishedFileId, bool bVoteUp) = 0;
    virtual SteamAPICall_t GetUserPublishedItemVoteDetails(PublishedFileId_t unPublishedFileId) = 0;
    virtual SteamAPICall_t EnumerateUserSharedWorkshopFiles(CSteamID steamId, uint32 unStartIndex, SteamParamStringArray_t *pRequiredTags, SteamParamStringArray_t *pExcludedTags) = 0;
    virtual SteamAPICall_t PublishVideo(EWorkshopVideoProvider eVideoProvider, const char *pchVideoAccount, const char *pchVideoIdentifier, const char *pchPreviewFile, AppId_t nConsumerAppId, const char *pchTitle, const char *pchDescription, ERemoteStoragePublishedFileVisibility eVisibility, SteamParamStringArray_t *pTags) = 0;
    virtual SteamAPICall_t SetUserPublishedFileAction(PublishedFileId_t unPublishedFileId, EWorkshopFileAction eAction) = 0;
    virtual SteamAPICall_t EnumeratePublishedFilesByUserAction(EWorkshopFileAction eAction, uint32 unStartIndex) = 0;
    virtual SteamAPICall_t EnumeratePublishedWorkshopFiles(EWorkshopEnumerationType eEnumerationType, uint32 unStartIndex, uint32 unCount, uint32 unDays, SteamParamStringArray_t *pTags, SteamParamStringArray_t *pUserTags) = 0;
    virtual SteamAPICall_t UGCDownloadToLocation(UGCHandle_t hContent, const char *pchLocation, uint32 unPriority) = 0;
};

#endif // ISTEAMREMOTESTORAGE014_H
