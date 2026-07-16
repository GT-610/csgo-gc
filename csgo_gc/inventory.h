#pragma once

#include "gc_const_csgo.h"
#include "item_schema.h"
#include "random.h"

class KeyValue;

using ItemMap = std::unordered_map<uint64_t, CSOEconItem>;

class Inventory
{
public:
    Inventory(uint64_t steamId);
    ~Inventory();

    struct ParameterizedItemOptions
    {
        struct TournamentOptions
        {
            std::optional<uint32_t> eventId;
            std::optional<uint32_t> stageId;
            std::optional<uint32_t> team0Id;
            std::optional<uint32_t> team1Id;
            std::optional<uint32_t> mvpAccountId;
        };

        std::optional<uint32_t> level;
        std::optional<uint32_t> quality;
        std::optional<uint32_t> rarity;
        std::optional<std::string> customName;

        std::optional<uint32_t> paint;
        std::optional<uint32_t> seed;
        std::optional<float> wear;
        std::optional<uint32_t> statTrak;
        std::optional<uint32_t> music;
        std::optional<uint32_t> sprayColor;
        std::optional<uint32_t> sprayRemaining;
        TournamentOptions tournament;

        std::array<std::optional<uint32_t>, 6> sticker;
        std::array<std::optional<float>, 6> stickerWear;
        std::array<std::optional<float>, 6> stickerScale;
        std::array<std::optional<float>, 6> stickerRotation;
    };

    void BuildCacheSubscription(CMsgSOCacheSubscribed &message, int level, bool server);

    bool EquipItem(uint64_t itemId, uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update);

    bool RemoveItem(uint64_t itemId, CMsgSOSingleObject &destroy);

    bool UseItem(uint64_t itemId,
        CMsgSOSingleObject &destroy,
        CMsgSOMultipleObjects &updateMultiple,
        CMsgGCItemCustomizationNotification &notification);

    bool UnlockCrate(uint64_t crateId,
        uint64_t keyId,
        CMsgSOSingleObject &destroyCrate,
        CMsgSOSingleObject &destroyKey,
        CMsgSOSingleObject &newItem,
        CMsgGCItemCustomizationNotification &notification);

    bool OpenSouvenirPackage(uint64_t packageId,
        CMsgSOSingleObject &destroyPackage,
        CMsgSOSingleObject &newItem,
        CMsgGCItemCustomizationNotification &notification);

    bool SetItemPositions(
        const CMsgSetItemPositions &message,
        std::vector<CMsgItemAcknowledged> &acknowledgements,
        CMsgSOMultipleObjects &update);

    bool ApplySticker(const CMsgApplySticker &message,
        CMsgSOSingleObject &update,
        CMsgSOSingleObject &destroy,
        CMsgGCItemCustomizationNotification &notification);

    bool ScrapeSticker(const CMsgApplySticker &message,
        CMsgSOSingleObject &update,
        CMsgSOSingleObject &destroy,
        CMsgGCItemCustomizationNotification &notification);

    uint64_t EquippedMusicKitItemId(bool statTrakOnly) const;
    uint32_t EquippedMusicKitMVPCount(bool incrementForLocalMVP) const;
    bool IncrementKillCountAttribute(uint64_t itemId, uint32_t amount, CMsgSOSingleObject &update);

    bool NameItem(uint64_t nameTagId,
        uint64_t itemId,
        std::string_view name,
        CMsgSOSingleObject &update,
        CMsgSOSingleObject &destroy,
        CMsgGCItemCustomizationNotification &notification);

    bool NameBaseItem(uint64_t nameTagId,
        uint32_t defIndex,
        std::string_view name,
        CMsgSOSingleObject &create,
        CMsgSOSingleObject &destroy,
        CMsgGCItemCustomizationNotification &notification);

    bool RemoveItemName(uint64_t itemId,
        CMsgSOSingleObject &update,
        CMsgSOSingleObject &destroy,
        CMsgGCItemCustomizationNotification &notification);

    enum class StorageResult
    {
        Success,
        CapacityExceeded,
        ItemNotFound,
        ContainerNotFound,
        InvalidContainerType,
        InternalError
    };

    struct StorageTransaction
    {
        CMsgSOSingleObject itemData;
        CMsgSOSingleObject containerData;
        EGCItemCustomizationNotification notificationType;
        uint64_t affectedContainerId;
        StorageResult outcome;
        
        bool Succeeded() const { return outcome == StorageResult::Success; }
        bool ReachedCapacity() const { return outcome == StorageResult::CapacityExceeded; }
    };

    StorageTransaction DepositItemToStorage(uint64_t storageId, uint64_t itemId);
    StorageTransaction WithdrawItemFromStorage(uint64_t storageId, uint64_t itemId);

    enum class CounterSwapStatus
    {
        Completed,
        ToolMissing,
        InvalidTool,
        WeaponMissing,
        CounterAttributeAbsent,
        InvalidWeaponState
    };

    struct CounterSwapResult
    {
        CounterSwapStatus status;
        CMsgSOSingleObject toolRemoval;
        CMsgSOSingleObject weaponAUpdate;
        CMsgSOSingleObject weaponBUpdate;
        uint64_t weaponAId;
        uint64_t weaponBId;
        
        bool IsValid() const { return status == CounterSwapStatus::Completed; }
    };

    CounterSwapResult PerformCounterSwap(uint64_t toolId, uint64_t weaponAId, uint64_t weaponBId);

    const CSOEconItem *GetItem(uint64_t itemId) const;
    const ItemSchema &GetItemSchema() const { return m_itemSchema; }

    // Trade-up contract: craft 10 items of same rarity into 1 item of next rarity
    // Returns true on success, false on validation failure
    bool TradeUp(const std::vector<uint64_t> &inputItemIds,
        std::vector<CMsgSOSingleObject> &destroyItems,
        CMsgSOSingleObject &newItem,
        CMsgGCItemCustomizationNotification &notification,
        CSOEconItem **outCraftedItem = nullptr);

    // returns the item id and adds the item to the provided CMsgSOMultipleObjects
    // on failure returns 0 and does nothing
    uint64_t PurchaseItem(uint32_t defIndex, std::vector<CMsgSOSingleObject> &update);
    uint64_t CreateParameterizedItem(uint32_t defIndex,
        const ParameterizedItemOptions &options,
        CMsgSOSingleObject &update,
        std::string &error);
    size_t ItemCount() const { return m_items.size(); }
    const ItemMap &Items() const { return m_items; }
    void Save() const { WriteToFile(); }

private:
    uint32_t AccountId() const;

    // allocates an empty item, sets id and account_id fields
    // pass zero as highItemId to generate a new one
    CSOEconItem &AllocateItem(uint32_t highItemId);

    // create a new item of a specific type
    CSOEconItem &CreateItem(const CSOEconItem &copyFrom);
    CSOEconItem &CreateItem(uint32_t defIndex, ItemOrigin origin, UnacknowledgedType unacknowledgedType);

    void ReadFromFile();
    void ReadItem(const KeyValue &itemKey, CSOEconItem &item) const;
    void LogInventoryConsistency() const;

    void WriteToFile() const;
    void WriteItem(KeyValue &itemKey, const CSOEconItem &item) const;

    // helper, only called via EquipItem
    bool UnequipItem(uint64_t itemId, CMsgSOMultipleObjects &update);
    void UnequipItem(uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update);

    void DestroyItem(ItemMap::iterator iterator, CMsgSOSingleObject &message);

    // move this to the item schema maybe?
    void ItemToPreviewDataBlock(const CSOEconItem &item, CEconItemPreviewDataBlock &block);

    // helpers for serializing items to CMsgSOMultipleObjects and CMsgSOSingleObject
    void AddToMultipleObjects(CMsgSOMultipleObjects &message, SOTypeId type, const google::protobuf::MessageLite &object);
    void ToSingleObject(CMsgSOSingleObject &message, SOTypeId type, const google::protobuf::MessageLite &object);

    // helpers for above..
    void AddToMultipleObjects(CMsgSOMultipleObjects &message, const CSOEconItem &object)
    {
        AddToMultipleObjects(message, SOTypeItem, object);
    }

    void ToSingleObject(CMsgSOSingleObject &message, const CSOEconItem &object)
    {
        ToSingleObject(message, SOTypeItem, object);
    }

    void AddToMultipleObjects(CMsgSOMultipleObjects &message, const CSOEconDefaultEquippedDefinitionInstanceClient &object)
    {
        AddToMultipleObjects(message, SOTypeDefaultEquippedDefinitionInstanceClient, object);
    }

    void ToSingleObject(CMsgSOSingleObject &message, const CSOEconDefaultEquippedDefinitionInstanceClient &object)
    {
        ToSingleObject(message, SOTypeDefaultEquippedDefinitionInstanceClient, object);
    }

    struct StorageItemPair { CSOEconItem* storage; CSOEconItem* target; };
    StorageItemPair ResolveStorageItems(uint64_t storageId, uint64_t targetId);
    void EmbedStorageReference(CSOEconItem &item, uint64_t storageId);
    void StripStorageReference(CSOEconItem &item);
    bool ModifyStorageCounter(CSOEconItem &storage, int delta);

    void ConsumeToolItem(uint64_t toolId, CMsgSOSingleObject &removalMsg);

    const uint64_t m_steamId;
    ItemSchema m_itemSchema;
    Random m_random;
    uint32_t m_lastHighItemId{};
    ItemMap m_items;
    std::vector<CSOEconDefaultEquippedDefinitionInstanceClient> m_defaultEquips;
};
