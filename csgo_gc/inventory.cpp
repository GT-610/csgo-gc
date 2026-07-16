#include "stdafx.h"
#include "inventory.h"
#include "case_opening.h"
#include "config.h"
#include "gc_const.h"
#include "item_utils.h"
#include "keyvalue.h"
#include "random.h"
#include "souvenir.h"

constexpr const char *InventoryFilePath = "csgo_gc/inventory.txt";

// mikkotodo actual versioning
constexpr uint64_t InventoryVersion = 7523377975160828514;

// mix the account id into item ids to avoid collisions in multiplayer games
inline uint64_t ComposeItemId(uint32_t accountId, uint32_t highItemId)
{
    uint64_t low = accountId;
    uint64_t high = highItemId;
    return low | (high << 32);
}

inline uint32_t HighItemId(uint64_t itemId)
{
    return (itemId >> 32);
}

// helper, see ItemIdDefaultItemMask for more information
inline bool IsDefaultItemId(uint64_t itemId, uint32_t &defIndex, uint32_t &paintKitIndex)
{
    if ((itemId & ItemIdDefaultItemMask) == ItemIdDefaultItemMask)
    {
        defIndex = itemId & 0xffff;
        paintKitIndex = (itemId >> 16) & 0xffff;
        return true;
    }

    return false;
}

static CSOEconItemAttribute *FindAttribute(CSOEconItem &item, uint32_t defIndex)
{
    for (int i = 0; i < item.attribute_size(); i++)
    {
        CSOEconItemAttribute *attribute = item.mutable_attribute(i);
        if (attribute->def_index() == defIndex)
        {
            return attribute;
        }
    }

    return nullptr;
}

static CSOEconItemAttribute *FindOrAddAttribute(CSOEconItem &item, uint32_t defIndex)
{
    if (CSOEconItemAttribute *attribute = FindAttribute(item, defIndex))
    {
        return attribute;
    }

    CSOEconItemAttribute *attribute = item.add_attribute();
    attribute->set_def_index(defIndex);
    return attribute;
}

uint32_t StickerIdAttribute(size_t slot)
{
    return ItemSchema::AttributeStickerId0 + static_cast<uint32_t>(slot) * 4;
}

uint32_t StickerWearAttribute(size_t slot)
{
    return ItemSchema::AttributeStickerWear0 + static_cast<uint32_t>(slot) * 4;
}

uint32_t StickerScaleAttribute(size_t slot)
{
    return ItemSchema::AttributeStickerScale0 + static_cast<uint32_t>(slot) * 4;
}

uint32_t StickerRotationAttribute(size_t slot)
{
    return ItemSchema::AttributeStickerRotation0 + static_cast<uint32_t>(slot) * 4;
}

Inventory::Inventory(uint64_t steamId)
    : m_steamId{ steamId }
{
    ReadFromFile();
}

Inventory::~Inventory()
{
    WriteToFile();
}

void Inventory::AddToMultipleObjects(CMsgSOMultipleObjects &message, SOTypeId type, const google::protobuf::MessageLite &object)
{
    if (!message.has_version())
    {
        assert(!message.has_owner_soid());
        message.set_version(InventoryVersion);
        message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
        message.mutable_owner_soid()->set_id(m_steamId);
    }
    else
    {
        assert(message.has_owner_soid());
    }

    CMsgSOMultipleObjects_SingleObject *single = message.add_objects_modified();
    single->set_type_id(type);
    single->set_object_data(object.SerializeAsString());
}

void Inventory::ToSingleObject(CMsgSOSingleObject &message, SOTypeId type, const google::protobuf::MessageLite &object)
{
    assert(!message.has_owner_soid());
    assert(!message.has_version());
    assert(!message.has_type_id());
    assert(!message.has_object_data());

    message.set_version(InventoryVersion);
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(m_steamId);

    message.set_type_id(type);
    message.set_object_data(object.SerializeAsString());
}

uint32_t Inventory::AccountId() const
{
    return m_steamId & 0xffffffff;
}

const CSOEconItem *Inventory::GetItem(uint64_t itemId) const
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        return nullptr;
    }

    return &it->second;
}

CSOEconItem &Inventory::AllocateItem(uint32_t highItemId)
{
    // Players fuck up their inventory files constantly and end up with item id collisions...
    // This doesn't return until the item id is unique for this session, try with the provided
    // item id first, if it's invalid or already in use increment it

    if (!highItemId)
    {
        m_lastHighItemId++;
        highItemId = m_lastHighItemId;
    }

    for (;; highItemId++)
    {
        uint64_t itemId = ComposeItemId(AccountId(), highItemId);
        if ((itemId & ItemIdDefaultItemMask) == ItemIdDefaultItemMask)
        {
            // would be interpreted as a default item (it's not)
            assert(false);
            // shit error handling
            continue;
        }

        auto [it, inserted] = m_items.try_emplace(itemId);
        if (!inserted)
        {
            // item id collision
            assert(false);
            continue;
        }

        if (highItemId > m_lastHighItemId)
        {
            m_lastHighItemId = highItemId;
        }

        // ok
        CSOEconItem &item = it->second;

        item.set_id(itemId);
        item.set_account_id(AccountId());

        return item;
    }
}

CSOEconItem &Inventory::CreateItem(const CSOEconItem &copyFrom)
{
    CSOEconItem &item = AllocateItem(0);

    // shitty but what can you do
    uint64_t itemId = item.id();
    uint32_t accountId = item.account_id();

    item = copyFrom;

    item.set_id(itemId);
    item.set_account_id(accountId);

    return item;
}

CSOEconItem &Inventory::CreateItem(uint32_t defIndex, ItemOrigin origin, UnacknowledgedType unacknowledgedType)
{
    CSOEconItem &item = AllocateItem(0);
    m_itemSchema.CreateItem(defIndex, origin, unacknowledgedType, item);
    return item;
}

void Inventory::ReadFromFile()
{
    KeyValue inventoryKey{ "inventory" };
    if (!inventoryKey.ParseFromFile(InventoryFilePath))
    {
        return;
    }

    const KeyValue *itemsKey = inventoryKey.GetSubkey("items");
    if (itemsKey)
    {
        m_items.reserve(itemsKey->SubkeyCount());

        for (const KeyValue &itemKey : *itemsKey)
        {
            uint32_t highItemId = FromString<uint32_t>(itemKey.Name());
            CSOEconItem &item = AllocateItem(highItemId);
            ReadItem(itemKey, item);
        }
    }

    const KeyValue *defaultEquipsKey = inventoryKey.GetSubkey("default_equips");
    if (defaultEquipsKey)
    {
        m_defaultEquips.reserve(defaultEquipsKey->SubkeyCount());

        for (const KeyValue &defaultEquipKey : *defaultEquipsKey)
        {
            CSOEconDefaultEquippedDefinitionInstanceClient &defaultEquip = m_defaultEquips.emplace_back();
            defaultEquip.set_account_id(AccountId());
            defaultEquip.set_item_definition(FromString<uint32_t>(defaultEquipKey.Name()));
            defaultEquip.set_class_id(defaultEquipKey.GetNumber<uint32_t>("class_id"));
            defaultEquip.set_slot_id(defaultEquipKey.GetNumber<uint32_t>("slot_id"));
        }
    }

    LogInventoryConsistency();
}

void Inventory::ReadItem(const KeyValue &itemKey, CSOEconItem &item) const
{
    // id and account_id were set by CreateItem
    item.set_inventory(itemKey.GetNumber<uint32_t>("inventory"));
    item.set_def_index(itemKey.GetNumber<uint32_t>("def_index"));
    item.set_quantity(1);
    item.set_level(itemKey.GetNumber<uint32_t>("level"));
    item.set_quality(itemKey.GetNumber<uint32_t>("quality"));
    item.set_flags(itemKey.GetNumber<uint32_t>("flags"));
    item.set_origin(itemKey.GetNumber<uint32_t>("origin"));

    std::string_view name = itemKey.GetString("custom_name");
    if (name.size())
    {
        item.set_custom_name(std::string{ name });
    }

    item.set_in_use(itemKey.GetNumber<int>("in_use"));
    item.set_rarity(itemKey.GetNumber<uint32_t>("rarity"));

    const KeyValue *attributesKey = itemKey.GetSubkey("attributes");
    if (attributesKey)
    {
        for (const KeyValue &attributeKey : *attributesKey)
        {
            CSOEconItemAttribute *attribute = item.add_attribute();

            uint32_t defIndex = FromString<uint32_t>(attributeKey.Name());
            attribute->set_def_index(defIndex);
            m_itemSchema.SetAttributeString(attribute, attributeKey.String());
        }
    }

    uint32_t paintKitDefIndex = 0;
    if (item.quality() == ItemSchema::QualityNormal
        && GetItemPaintKitDefIndex(item, m_itemSchema, paintKitDefIndex))
    {
        item.set_quality(ItemSchema::QualityUnique);
    }

    const KeyValue *equippedStateKey = itemKey.GetSubkey("equipped_state");
    if (equippedStateKey)
    {
        for (const KeyValue &equippedKey : *equippedStateKey)
        {
            CSOEconItemEquipped *equipped = item.add_equipped_state();
            equipped->set_new_class(FromString<uint32_t>(equippedKey.Name()));
            equipped->set_new_slot(FromString<uint32_t>(equippedKey.String()));
        }
    }
}

void Inventory::WriteToFile() const
{
    KeyValue inventoryKey{ "inventory" };

    {
        KeyValue &itemsKey = inventoryKey.AddSubkey("items");

        for (const auto &pair : m_items)
        {
            const CSOEconItem &item = pair.second;
            KeyValue &itemKey = itemsKey.AddSubkey(std::to_string(HighItemId(item.id())));
            WriteItem(itemKey, item);
        }
    }

    {
        KeyValue &defaultEquipsKey = inventoryKey.AddSubkey("default_equips");

        for (const CSOEconDefaultEquippedDefinitionInstanceClient &defaultEquip : m_defaultEquips)
        {
            KeyValue &defaultEquipKey = defaultEquipsKey.AddSubkey(std::to_string(defaultEquip.item_definition()));
            defaultEquipKey.AddNumber("class_id", defaultEquip.class_id());
            defaultEquipKey.AddNumber("slot_id", defaultEquip.slot_id());
        }
    }

    inventoryKey.WriteToFile(InventoryFilePath);
}

void Inventory::WriteItem(KeyValue &itemKey, const CSOEconItem &item) const
{
    itemKey.AddNumber("inventory", item.inventory());
    itemKey.AddNumber("def_index", item.def_index());
    itemKey.AddNumber("level", item.level());
    itemKey.AddNumber("quality", item.quality());
    itemKey.AddNumber("flags", item.flags());
    itemKey.AddNumber("origin", item.origin());

    itemKey.AddString("custom_name", item.custom_name());

    itemKey.AddNumber("in_use", item.in_use());
    itemKey.AddNumber("rarity", item.rarity());

    KeyValue &attributesKey = itemKey.AddSubkey("attributes");
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        std::string name = std::to_string(attribute.def_index());
        std::string value = m_itemSchema.AttributeString(&attribute);
        attributesKey.AddString(name, value);
    }

    KeyValue &equippedStateKey = itemKey.AddSubkey("equipped_state");
    for (const CSOEconItemEquipped &equip : item.equipped_state())
    {
        equippedStateKey.AddNumber(std::to_string(equip.new_class()), equip.new_slot());
    }
}

void Inventory::BuildCacheSubscription(CMsgSOCacheSubscribed &message, int level, bool server)
{
    message.set_version(InventoryVersion);
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(m_steamId);

    {
        CMsgSOCacheSubscribed_SubscribedType *object = message.add_objects();
        object->set_type_id(SOTypeItem);

        for (const auto &pair : m_items)
        {
            if (server && !pair.second.equipped_state_size())
            {
                continue;
            }

            object->add_object_data(pair.second.SerializeAsString());
        }
    }

    {
        CSOPersonaDataPublic personaData;
        personaData.set_player_level(level);
        personaData.set_elevated_state(true);

        CMsgSOCacheSubscribed_SubscribedType *object = message.add_objects();
        object->set_type_id(SOTypePersonaDataPublic);
        object->add_object_data(personaData.SerializeAsString());
    }

    if (!server)
    {
        CSOEconGameAccountClient accountClient;
        accountClient.set_additional_backpack_slots(0);
        accountClient.set_bonus_xp_timestamp_refresh(static_cast<uint32_t>(time(nullptr)));
        accountClient.set_bonus_xp_usedflags(16); // caught cheater lobbies, overwatch bonus etc
        accountClient.set_elevated_state(ElevatedStatePrime);
        accountClient.set_elevated_timestamp(ElevatedStatePrime); // is this actually 5????

        CMsgSOCacheSubscribed_SubscribedType *object = message.add_objects();
        object->set_type_id(SOTypeGameAccountClient);
        object->add_object_data(accountClient.SerializeAsString());
    }

    {
        CMsgSOCacheSubscribed_SubscribedType *object = message.add_objects();
        object->set_type_id(SOTypeDefaultEquippedDefinitionInstanceClient);

        for (const CSOEconDefaultEquippedDefinitionInstanceClient &defaultEquip : m_defaultEquips)
        {
            object->add_object_data(defaultEquip.SerializeAsString());
        }
    }
}

constexpr uint32_t SlotUneqip = 0xffff;
constexpr uint64_t ItemIdInvalid = 0;

// yes this function is inefficent!!! but i think that makes it more clear
// also i think this is the way valve gc does it???? can't remember
bool Inventory::EquipItem(uint64_t itemId, uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update)
{
    if (slotId == SlotUneqip)
    {
        // unequipping a specific item from all slots
        return UnequipItem(itemId, update);
    }

    assert(itemId);
    assert(itemId != UINT64_MAX);

    if (itemId == ItemIdInvalid)
    {
        // unequip from this slot, itemid not provided so nothing gets equipped
        UnequipItem(classId, slotId, update);
        return true;
    }

    uint32_t defIndex, paintKitIndex;
    if (IsDefaultItemId(itemId, defIndex, paintKitIndex))
    {
        // if an item is equipped in this slot, unequip it first
        UnequipItem(classId, slotId, update);

        Platform::Print("EquipItem def %u class %d slot %d\n", defIndex, classId, slotId);

        CSOEconDefaultEquippedDefinitionInstanceClient &defaultEquip = m_defaultEquips.emplace_back();
        defaultEquip.set_account_id(AccountId());
        defaultEquip.set_item_definition(defIndex);
        defaultEquip.set_class_id(classId);
        defaultEquip.set_slot_id(slotId);

        AddToMultipleObjects(update, defaultEquip);

        return true;
    }
    else
    {
        auto it = m_items.find(itemId);
        if (it == m_items.end())
        {
            Platform::Print("EquipItem: no such item %llu!!!!\n", itemId);
            return false; // didn't modify anything
        }

        // if an item is equipped in this slot, unequip it first
        UnequipItem(classId, slotId, update);

        Platform::Print("EquipItem %llu class %d slot %d\n", itemId, classId,
            slotId);

        CSOEconItem &item = it->second;

        CSOEconItemEquipped *equippedState = item.add_equipped_state();
        equippedState->set_new_class(classId);
        equippedState->set_new_slot(slotId);

        AddToMultipleObjects(update, item);

        return true;
    }
}

bool Inventory::RemoveItem(uint64_t itemId, CMsgSOSingleObject &response)
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    DestroyItem(it, response);
    return true;
}

bool Inventory::UseItem(uint64_t itemId,
    CMsgSOSingleObject &destroy,
    CMsgSOMultipleObjects &updateMultiple,
    CMsgGCItemCustomizationNotification &notification)
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    if (it->second.def_index() != ItemSchema::ItemSpray)
    {
        assert(false);
        return false;
    }

    // create an unsealed spray based on the sealed one
    CSOEconItem &unsealed = CreateItem(it->second);
    unsealed.set_def_index(ItemSchema::ItemSprayPaint);

    // remove the sealed spray from our inventory
    DestroyItem(it, destroy);

    // equip the new spray, this will also unequip the old one if we had one
    EquipItem(unsealed.id(), 0, ItemSchema::LoadoutSlotGraffiti, updateMultiple);

    // remove this to have unlimited sprays
    CSOEconItemAttribute *attribute = unsealed.add_attribute();
    attribute->set_def_index(ItemSchema::AttributeSpraysRemaining);
    m_itemSchema.SetAttributeUint32(attribute, 50);

    // set notification
    notification.add_item_id(unsealed.id());
    notification.set_request(k_EGCItemCustomizationNotification_GraffitiUnseal);

    return true;
}

bool Inventory::UnlockCrate(uint64_t crateId,
    uint64_t keyId,
    CMsgSOSingleObject &destroyCrate,
    CMsgSOSingleObject &destroyKey,
    CMsgSOSingleObject &newItem,
    CMsgGCItemCustomizationNotification &notification)
{
    auto crate = m_items.find(crateId);
    if (crate == m_items.end())
    {
        assert(false);
        return false;
    }

    if (keyId)
    {
        auto key = m_items.find(keyId);
        if (key == m_items.end())
        {
            Platform::Print("UnlockCrate: key item %llu not found for crate %llu\n", keyId, crateId);
            return false;
        }

        if (!m_itemSchema.IsKeyToolDefIndex(key->second.def_index()))
        {
            Platform::Print("UnlockCrate: item %llu def %u is not a key tool for crate %llu\n",
                keyId, key->second.def_index(), crateId);
            return false;
        }

        if (!m_itemSchema.IsKeyCompatibleWithCrate(key->second.def_index(), crate->second.def_index()))
        {
            Platform::Print("UnlockCrate: key item %llu def %u is not compatible with crate %llu def %u\n",
                keyId, key->second.def_index(), crateId, crate->second.def_index());
            return false;
        }
    }

    // CASE OPENING
    CaseOpening caseOpening{ m_itemSchema, m_random };

    CSOEconItem temp;
    if (!caseOpening.SelectItemFromCrate(crate->second, temp))
    {
        assert(false);
        return false;
    }

    CSOEconItem &item = CreateItem(temp);

    ToSingleObject(newItem, item);

    // set notification
    notification.add_item_id(item.id());
    notification.set_request(k_EGCItemCustomizationNotification_UnlockCrate);

    // remove the crate
    if (GetConfig().DestroyUsedItems())
    {
        DestroyItem(crate, destroyCrate);

        auto key = m_items.find(keyId);
        if (keyId && key != m_items.end())
        {
            DestroyItem(key, destroyKey);
        }
    }

    return true;
}

bool Inventory::OpenSouvenirPackage(uint64_t packageId,
    CMsgSOSingleObject &destroyPackage,
    CMsgSOSingleObject &newItem,
    CMsgGCItemCustomizationNotification &notification)
{
    auto package = m_items.find(packageId);
    if (package == m_items.end())
    {
        Platform::Print("OpenSouvenirPackage: package %llu not found\n", packageId);
        return false;
    }

    SouvenirOpening souvenirOpening{ m_itemSchema, m_random };

    CSOEconItem temp;
    if (!souvenirOpening.OpenPackage(package->second, temp))
    {
        Platform::Print("OpenSouvenirPackage: failed to open package\n");
        return false;
    }

    CSOEconItem &item = CreateItem(temp);

    ToSingleObject(newItem, item);

    // set notification
    notification.add_item_id(item.id());
    notification.set_request(k_EGCItemCustomizationNotification_GenerateSouvenir);

    // remove the package
    if (GetConfig().DestroyUsedItems())
    {
        DestroyItem(package, destroyPackage);
    }

    return true;
}

static int ItemWearLevel(float wearFloat)
{
    if (wearFloat < 0.07f)
    {
        // factory new
        return 0;
    }

    if (wearFloat < 0.15f)
    {
        // minimal wear
        return 1;
    }

    if (wearFloat < 0.37f)
    {
        // field tested
        return 2;
    }

    if (wearFloat < 0.45f)
    {
        // well worn
        return 3;
    }

    // battle scarred
    return 4;
}


void Inventory::ItemToPreviewDataBlock(const CSOEconItem &item, CEconItemPreviewDataBlock &block)
{
    block.set_accountid(item.account_id());
    block.set_itemid(item.id());
    block.set_defindex(item.def_index());
    block.set_rarity(item.rarity());
    block.set_quality(item.quality());
    block.set_customname(item.custom_name());
    block.set_inventory(item.inventory());
    block.set_origin(item.origin());

    std::array<CEconItemPreviewDataBlock_Sticker, MaxStickers> stickers;

    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        uint32_t defIndex = attribute.def_index();
        switch (defIndex)
        {
        case ItemSchema::AttributeTexturePrefab:
            block.set_paintindex(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeTextureSeed:
            block.set_paintseed(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeTextureWear:
        {
            int wearLevel = ItemWearLevel(m_itemSchema.AttributeFloat(&attribute));
            block.set_paintwear(wearLevel);
            break;
        }

        case ItemSchema::AttributeKillEater:
            block.set_killeatervalue(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeKillEaterScoreType:
            block.set_killeaterscoretype(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeMusicId:
            block.set_musicindex(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeQuestId:
            block.set_questid(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeSprayTintId:
            stickers[0].set_tint_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerId0:
            stickers[0].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear0:
            stickers[0].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale0:
            stickers[0].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation0:
            stickers[0].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerId1:
            stickers[1].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear1:
            stickers[1].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale1:
            stickers[1].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation1:
            stickers[1].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerId2:
            stickers[2].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear2:
            stickers[2].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale2:
            stickers[2].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation2:
            stickers[2].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerId3:
            stickers[3].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear3:
            stickers[3].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale3:
            stickers[3].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation3:
            stickers[3].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerId4:
            stickers[4].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear4:
            stickers[4].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale4:
            stickers[4].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation4:
            stickers[4].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerId5:
            stickers[5].set_sticker_id(m_itemSchema.AttributeUint32(&attribute));
            break;

        case ItemSchema::AttributeStickerWear5:
            stickers[5].set_wear(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerScale5:
            stickers[5].set_scale(m_itemSchema.AttributeFloat(&attribute));
            break;

        case ItemSchema::AttributeStickerRotation5:
            stickers[5].set_rotation(m_itemSchema.AttributeFloat(&attribute));
            break;
        }
    }

    for (size_t i = 0; i < stickers.size(); i++)
    {
        const CEconItemPreviewDataBlock_Sticker &source = stickers[i];
        if (!source.has_sticker_id())
        {
            continue;
        }

        CEconItemPreviewDataBlock_Sticker *sticker = block.add_stickers();
        *sticker = source;
        sticker->set_slot(i);
    }
}

bool Inventory::SetItemPositions(
    const CMsgSetItemPositions &message,
    std::vector<CMsgItemAcknowledged> &acknowledgements,
    CMsgSOMultipleObjects &update)
{
    for (const CMsgSetItemPositions_ItemPosition &position : message.item_positions())
    {
        auto it = m_items.find(position.item_id());
        if (it == m_items.end())
        {
            assert(false);
            return false;
        }

        CSOEconItem &item = it->second;

        Platform::Print("SetItemPositions: %llu --> %u\n", position.item_id(), position.position());

        CMsgItemAcknowledged &acknowledgement = acknowledgements.emplace_back();
        ItemToPreviewDataBlock(item, *acknowledgement.mutable_iteminfo());

        item.set_inventory(position.position());

        AddToMultipleObjects(update, item);
    }

    return true;
}

bool Inventory::ApplySticker(const CMsgApplySticker &message,
    CMsgSOSingleObject &update,
    CMsgSOSingleObject &destroy,
    CMsgGCItemCustomizationNotification &notification)
{
    assert(message.has_sticker_item_id());
    assert(message.has_sticker_slot());
    assert(!message.has_sticker_wear());

    auto sticker = m_items.find(message.sticker_item_id());
    if (sticker == m_items.end())
    {
        assert(false);
        return false;
    }

    if (message.sticker_slot() >= MaxStickers)
    {
        Platform::Print("ApplySticker: invalid slot %u for tool %llu\n",
            message.sticker_slot(), message.sticker_item_id());
        return false;
    }

    if (sticker->second.def_index() != ItemSchema::ItemSticker
        && sticker->second.def_index() != ItemSchema::ItemPatch)
    {
        Platform::Print("ApplySticker: item %llu def %u is not a sticker or patch tool\n",
            message.sticker_item_id(), sticker->second.def_index());
        return false;
    }

    uint32_t targetDefIndex = 0;
    CSOEconItem *item = nullptr;
    if (message.baseitem_defidx())
    {
        targetDefIndex = message.baseitem_defidx();
    }
    else
    {
        auto it = m_items.find(message.item_item_id());
        if (it == m_items.end())
        {
            assert(false);
            return false;
        }

        targetDefIndex = it->second.def_index();
        item = &it->second;
    }

    if (sticker->second.def_index() == ItemSchema::ItemPatch)
    {
        if (!m_itemSchema.CanApplyPatchToDefIndex(targetDefIndex))
        {
            Platform::Print("ApplySticker: patch item %llu cannot be applied to target def %u (target item %llu)\n",
                message.sticker_item_id(), targetDefIndex, message.item_item_id());
            return false;
        }
    }
    else if (!m_itemSchema.CanApplyStickerToDefIndex(targetDefIndex))
    {
        Platform::Print("ApplySticker: sticker item %llu cannot be applied to target def %u (target item %llu)\n",
            message.sticker_item_id(), targetDefIndex, message.item_item_id());
        return false;
    }

    if (message.baseitem_defidx())
    {
        item = &CreateItem(message.baseitem_defidx(), ItemOriginBaseItem, UnacknowledgedInvalid);
    }

    assert(item);

    // get the sticker kit def index
    uint32_t stickerKit = 0;

    for (const CSOEconItemAttribute &attribute : sticker->second.attribute())
    {
        if (attribute.def_index() == ItemSchema::AttributeStickerId0)
        {
            stickerKit = m_itemSchema.AttributeUint32(&attribute);
            break;
        }
    }

    if (!stickerKit)
    {
        Platform::Print("ApplySticker: item %llu def %u has no sticker kit attribute\n",
            message.sticker_item_id(), sticker->second.def_index());
        assert(false);
        return false;
    }

    uint32_t attributeStickerId = ItemSchema::AttributeStickerId0 + (message.sticker_slot() * 4);
    uint32_t attributeStickerWear = ItemSchema::AttributeStickerWear0 + (message.sticker_slot() * 4);

    // add the sticker id attribute
    CSOEconItemAttribute *attribute = item->add_attribute();
    attribute->set_def_index(attributeStickerId);
    m_itemSchema.SetAttributeUint32(attribute, stickerKit);

    // add the sticker wear attribute if this is not a patch
    if (sticker->second.def_index() != ItemSchema::ItemPatch)
    {
        attribute = item->add_attribute();
        attribute->set_def_index(attributeStickerWear);
        m_itemSchema.SetAttributeFloat(attribute, 0);
    }

    ToSingleObject(update, *item);

    // remove the sticker
    if (GetConfig().DestroyUsedItems())
    {
        DestroyItem(sticker, destroy);
    }

    // notification, if any
    notification.add_item_id(item->id());
    notification.set_request(k_EGCItemCustomizationNotification_ApplySticker);

    return true;
}

static void RemoveStickerAttributes(CSOEconItem &item, uint32_t slot)
{
    // mikkotodo lookup table instead of this crap...
    // mikkotodo rest of attribs???
    uint32_t attributeStickerId = ItemSchema::AttributeStickerId0 + (slot * 4);
    uint32_t attributeStickerWear = ItemSchema::AttributeStickerWear0 + (slot * 4);

    for (auto attrib = item.mutable_attribute()->begin(); attrib != item.mutable_attribute()->end();)
    {
        if (attrib->def_index() == attributeStickerId
            || attrib->def_index() == attributeStickerWear)
        {
            attrib = item.mutable_attribute()->erase(attrib);
        }
        else
        {
            attrib++;
        }
    }
}

bool Inventory::ScrapeSticker(const CMsgApplySticker &message,
    CMsgSOSingleObject &update,
    CMsgSOSingleObject &destroy,
    CMsgGCItemCustomizationNotification &notification)
{
    auto it = m_items.find(message.item_item_id());
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    CSOEconItem &item = it->second;

    // mikkotodo lookup table instead of this crap...
    uint32_t attributeStickerWear = ItemSchema::AttributeStickerWear0 + (message.sticker_slot() * 4);

    CSOEconItemAttribute *wearAttribute = nullptr;
    for (int i = 0; i < item.attribute_size(); i++)
    {
        if (item.mutable_attribute(i)->def_index() == attributeStickerWear)
        {
            wearAttribute = item.mutable_attribute(i);
            break;
        }
    }

    float wearLevel = 0.0f;

    if (wearAttribute)
    {
        // TODO: randomize wear increment instead of using fixed 1/9 step
        float wearIncrement = 1.0f / 9;
        wearLevel = m_itemSchema.AttributeFloat(wearAttribute) + wearIncrement;
    }

    // if the wear attribute is not present, remove it outright (patches)
    if (!wearAttribute || wearLevel > 1.0f)
    {
        // so long, and thanks for all the fish

        // mikkotodo fix... should this be deduced from the item???
        uint32_t request = k_EGCItemCustomizationNotification_RemoveSticker;
        if (!wearAttribute)
        {
            request = k_EGCItemCustomizationNotification_RemovePatch;
        }

        if (item.rarity() == ItemSchema::RarityDefault)
        {
            // sticker removal notification with a fake item id
            notification.add_item_id(item.def_index() | ItemIdDefaultItemMask);
            notification.set_request(request);

            // this was a default weapon clone with a sticker so destroy the entire item
            DestroyItem(it, destroy);
        }
        else
        {
            // sticker removal notification
            notification.add_item_id(item.id());
            notification.set_request(request);

            // remove the sticker
            RemoveStickerAttributes(item, message.sticker_slot());

            ToSingleObject(update, item);
        }
    }
    else
    {
        // just update the wear
        m_itemSchema.SetAttributeFloat(wearAttribute, wearLevel);

        ToSingleObject(update, item);
    }

    return true;
}

bool Inventory::IncrementKillCountAttribute(uint64_t itemId, uint32_t amount, CMsgSOSingleObject &update)
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    CSOEconItem &item = it->second;
    bool incremented = false;

    for (int i = 0; i < item.attribute_size(); i++)
    {
        CSOEconItemAttribute *attribute = item.mutable_attribute(i);
        if (attribute->def_index() == ItemSchema::AttributeKillEater)
        {
            int value = m_itemSchema.AttributeUint32(attribute) + amount;
            m_itemSchema.SetAttributeUint32(attribute, value);
            incremented = true;
            break;
        }
    }

    if (incremented)
    {
        ToSingleObject(update, item);
        return true;
    }

    assert(false);
    return false;
}

static bool IsEquippedMusicKitItem(const CSOEconItem &item)
{
    // Music kits live in the shared no-team loadout.
    for (const CSOEconItemEquipped &equipped : item.equipped_state())
    {
        if (equipped.new_class() == 0
            && equipped.new_slot() == ItemSchema::LoadoutSlotMusicKit)
        {
            return true;
        }
    }

    return false;
}

uint64_t Inventory::EquippedMusicKitItemId(bool statTrakOnly) const
{
    for (const auto &pair : m_items)
    {
        const CSOEconItem &item = pair.second;
        if (!IsEquippedMusicKitItem(item))
        {
            continue;
        }

        bool hasKillEater = false;
        bool isMusicKitScoreType = false;

        for (const CSOEconItemAttribute &attribute : item.attribute())
        {
            if (attribute.def_index() == ItemSchema::AttributeKillEater)
            {
                hasKillEater = true;
            }
            else if (attribute.def_index() == ItemSchema::AttributeKillEaterScoreType
                && m_itemSchema.AttributeUint32(&attribute) == 1)
            {
                isMusicKitScoreType = true;
            }
        }

        if (!hasKillEater)
        {
            if (statTrakOnly)
            {
                continue;
            }

            return item.id();
        }

        if (isMusicKitScoreType)
        {
            return item.id();
        }
    }

    return 0;
}

uint32_t Inventory::EquippedMusicKitMVPCount(bool incrementForLocalMVP) const
{
    uint64_t itemId = EquippedMusicKitItemId(true);
    if (!itemId)
    {
        return 0;
    }

    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        return 0;
    }

    for (const CSOEconItemAttribute &attribute : it->second.attribute())
    {
        if (attribute.def_index() == ItemSchema::AttributeKillEater)
        {
            uint32_t count = m_itemSchema.AttributeUint32(&attribute);
            return incrementForLocalMVP ? count + 1 : count;
        }
    }

    return 0;
}

static bool PrepareCasketForNaming(ItemSchema &schema, CSOEconItem &casket)
{
    assert(casket.def_index() == ItemSchema::ItemCasket);

    if (!FindAttribute(casket, ItemSchema::AttributeCasketItemsCount))
    {
        CSOEconItemAttribute *countAttribute = casket.add_attribute();
        countAttribute->set_def_index(ItemSchema::AttributeCasketItemsCount);
        if (!schema.SetAttributeUint32(countAttribute, 0))
        {
            return false;
        }
    }

    CSOEconItemAttribute *dateAttribute =
        FindOrAddAttribute(casket, ItemSchema::AttributeCasketModificationDate);
    return schema.SetAttributeUint32(dateAttribute, static_cast<uint32_t>(time(nullptr)));
}

bool Inventory::NameItem(uint64_t nameTagId,
    uint64_t itemId,
    std::string_view name,
    CMsgSOSingleObject &update,
    CMsgSOSingleObject &destroy,
    CMsgGCItemCustomizationNotification &notification)
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        Platform::Print("NameItem: target item %llu not found\n", itemId);
        return false;
    }

    // The client sends the casket's built-in naming action as NameItem with a
    // zero tool id. It does not use an inventory Name Tag or the ordinary
    // item-schema nameable capability.
    const bool isCasketNaming =
        nameTagId == 0 && it->second.def_index() == ItemSchema::ItemCasket;

    auto tag = m_items.end();
    if (isCasketNaming)
    {
        if (!PrepareCasketForNaming(m_itemSchema, it->second))
        {
            Platform::Print("NameItem: failed to initialize casket %llu\n", itemId);
            return false;
        }
    }
    else
    {
        if (!m_itemSchema.CanNameDefIndex(it->second.def_index()))
        {
            Platform::Print("NameItem: target %llu def %u is not nameable by name tag %llu\n",
                itemId, it->second.def_index(), nameTagId);
            return false;
        }

        tag = m_items.find(nameTagId);
        if (tag == m_items.end())
        {
            Platform::Print("NameItem: name tag item %llu not found for target %llu\n",
                nameTagId, itemId);
            return false;
        }

        if (!m_itemSchema.IsNameTagToolDefIndex(tag->second.def_index()))
        {
            Platform::Print("NameItem: item %llu def %u is not a name tag for target %llu\n",
                nameTagId, tag->second.def_index(), itemId);
            return false;
        }
    }

    it->second.mutable_custom_name()->assign(name);

    ToSingleObject(update, it->second);

    if (GetConfig().DestroyUsedItems() && tag != m_items.end())
    {
        DestroyItem(tag, destroy);
    }

    notification.add_item_id(it->second.id());
    notification.set_request(k_EGCItemCustomizationNotification_NameItem);

    return true;
}

bool Inventory::NameBaseItem(uint64_t nameTagId,
    uint32_t defIndex,
    std::string_view name,
    CMsgSOSingleObject &create,
    CMsgSOSingleObject &destroy,
    CMsgGCItemCustomizationNotification &notification)
{
    auto tag = m_items.find(nameTagId);
    if (tag == m_items.end())
    {
        Platform::Print("NameBaseItem: name tag item %llu not found for base def %u\n",
            nameTagId, defIndex);
        return false;
    }

    if (!m_itemSchema.IsNameTagToolDefIndex(tag->second.def_index()))
    {
        Platform::Print("NameBaseItem: item %llu def %u is not a name tag for base def %u\n",
            nameTagId, tag->second.def_index(), defIndex);
        return false;
    }

    if (!m_itemSchema.CanNameDefIndex(defIndex))
    {
        Platform::Print("NameBaseItem: base def %u is not nameable by name tag %llu\n",
            defIndex, nameTagId);
        return false;
    }

    CSOEconItem &item = CreateItem(defIndex, ItemOriginBaseItem, UnacknowledgedInvalid);

    item.mutable_custom_name()->assign(name);

    ToSingleObject(create, item);

    if (GetConfig().DestroyUsedItems())
    {
        DestroyItem(tag, destroy);
    }

    notification.add_item_id(item.id()); // mikkotodo def index???
    notification.set_request(k_EGCItemCustomizationNotification_NameBaseItem);

    return true;
}

bool Inventory::RemoveItemName(uint64_t itemId,
    CMsgSOSingleObject &update,
    CMsgSOSingleObject &destroy,
    CMsgGCItemCustomizationNotification &notification)
{
    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    if (it->second.rarity() == ItemSchema::RarityDefault)
    {
        notification.add_item_id(it->second.def_index() | ItemIdDefaultItemMask);
        notification.set_request(k_EGCItemCustomizationNotification_RemoveItemName);

        DestroyItem(it, destroy);
    }
    else
    {
        it->second.mutable_custom_name()->clear();

        notification.add_item_id(it->second.id());
        notification.set_request(k_EGCItemCustomizationNotification_RemoveItemName);

        ToSingleObject(update, it->second);
    }

    return true;
}

Inventory::StorageItemPair Inventory::ResolveStorageItems(uint64_t storageId, uint64_t targetId)
{
    StorageItemPair result{ nullptr, nullptr };
    
    auto storageIt = m_items.find(storageId);
    auto targetIt = m_items.find(targetId);
    
    if (storageIt != m_items.end())
        result.storage = &storageIt->second;
    if (targetIt != m_items.end())
        result.target = &targetIt->second;
    
    return result;
}

void Inventory::EmbedStorageReference(CSOEconItem &item, uint64_t storageId)
{
    StripStorageReference(item);

    auto *attrLow = item.add_attribute();
    auto *attrHigh = item.add_attribute();
    
    attrLow->set_def_index(ItemSchema::AttributeCasketIdLow);
    attrHigh->set_def_index(ItemSchema::AttributeCasketIdHigh);
    
    m_itemSchema.SetAttributeUint32(attrLow, storageId & 0xFFFFFFFF);
    m_itemSchema.SetAttributeUint32(attrHigh, storageId >> 32);
    
    item.clear_equipped_state();
}

void Inventory::StripStorageReference(CSOEconItem &item)
{
    auto *attrs = item.mutable_attribute();
    
    for (int i = attrs->size() - 1; i >= 0; --i)
    {
        uint32_t idx = attrs->Get(i).def_index();
        bool isStorageAttr = (idx == ItemSchema::AttributeCasketIdLow || 
                              idx == ItemSchema::AttributeCasketIdHigh);
        if (isStorageAttr)
            attrs->DeleteSubrange(i, 1);
    }
}

static std::optional<uint64_t> StorageReference(const CSOEconItem &item, const ItemSchema &schema)
{
    std::optional<uint32_t> low;
    std::optional<uint32_t> high;

    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        switch (attribute.def_index())
        {
        case ItemSchema::AttributeCasketIdLow:
            low = schema.AttributeUint32(&attribute);
            break;

        case ItemSchema::AttributeCasketIdHigh:
            high = schema.AttributeUint32(&attribute);
            break;
        }
    }

    if (!low.has_value() || !high.has_value())
    {
        return std::nullopt;
    }

    return static_cast<uint64_t>(*low) | (static_cast<uint64_t>(*high) << 32);
}

void Inventory::LogInventoryConsistency() const
{
    size_t issueCount = 0;
    std::unordered_map<uint64_t, uint32_t> storedItemCountByCasket;
    std::unordered_map<uint64_t, uint32_t> configuredCasketCountById;

    for (const auto &pair : m_items)
    {
        const CSOEconItem &item = pair.second;
        if (item.def_index() != ItemSchema::ItemCasket)
        {
            continue;
        }

        bool hasCount = false;
        for (const CSOEconItemAttribute &attribute : item.attribute())
        {
            if (attribute.def_index() == ItemSchema::AttributeCasketItemsCount)
            {
                configuredCasketCountById[item.id()] = m_itemSchema.AttributeUint32(&attribute);
                hasCount = true;
                break;
            }
        }

        if (!hasCount)
        {
            issueCount++;
            Platform::Print("InventoryCheck: casket %llu has no item count attribute\n", item.id());
        }
    }

    for (const auto &pair : m_items)
    {
        const CSOEconItem &item = pair.second;
        const ItemInfo *itemInfo = m_itemSchema.ItemInfoByDefIndex(item.def_index());

        if (!itemInfo)
        {
            issueCount++;
            Platform::Print("InventoryCheck: item %llu has unknown def_index %u\n",
                item.id(), item.def_index());
        }

        int paintKitAttributes = 0;
        int wearAttributes = 0;
        int killEaterAttributes = 0;
        int scoreTypeAttributes = 0;
        int casketLowAttributes = 0;
        int casketHighAttributes = 0;
        uint32_t paintKitDefIndex = 0;
        uint32_t scoreType = 0;

        for (const CSOEconItemAttribute &attribute : item.attribute())
        {
            switch (attribute.def_index())
            {
            case ItemSchema::AttributeTexturePrefab:
                paintKitDefIndex = m_itemSchema.AttributeUint32(&attribute);
                paintKitAttributes++;
                break;

            case ItemSchema::AttributeTextureWear:
                wearAttributes++;
                break;

            case ItemSchema::AttributeKillEater:
                killEaterAttributes++;
                break;

            case ItemSchema::AttributeKillEaterScoreType:
                scoreType = m_itemSchema.AttributeUint32(&attribute);
                scoreTypeAttributes++;
                break;

            case ItemSchema::AttributeCasketIdLow:
                casketLowAttributes++;
                break;

            case ItemSchema::AttributeCasketIdHigh:
                casketHighAttributes++;
                break;
            }
        }

        if (paintKitAttributes > 1)
        {
            issueCount++;
            Platform::Print("InventoryCheck: painted item %llu has %d paint kit attributes\n",
                item.id(), paintKitAttributes);
        }

        if (paintKitAttributes > 0)
        {
            const PaintKitInfo *paintKitInfo = m_itemSchema.PaintKitInfoByDefIndex(paintKitDefIndex);
            if (!paintKitInfo)
            {
                issueCount++;
                Platform::Print("InventoryCheck: painted item %llu uses unknown paint kit %u (def %u)\n",
                    item.id(), paintKitDefIndex, item.def_index());
            }

            uint32_t paintedRarity = m_itemSchema.GetPaintedRarity(
                item.def_index(),
                paintKitDefIndex,
                item.rarity());
            if (item.rarity() != paintedRarity)
            {
                issueCount++;
                Platform::Print("InventoryCheck: painted item %llu has stored rarity %u but computed painted rarity %u (def %u, paint %u)\n",
                    item.id(), item.rarity(), paintedRarity, item.def_index(), paintKitDefIndex);
            }

            if (wearAttributes == 0)
            {
                issueCount++;
                Platform::Print("InventoryCheck: painted item %llu has no wear attribute (def %u, paint %u)\n",
                    item.id(), item.def_index(), paintKitDefIndex);
            }
            else if (wearAttributes > 1)
            {
                issueCount++;
                Platform::Print("InventoryCheck: painted item %llu has %d wear attributes\n",
                    item.id(), wearAttributes);
            }

            std::vector<std::string> collections;
            if (!m_itemSchema.GetCollectionsForPaintedItem(item.def_index(), paintKitDefIndex, collections)
                && !m_itemSchema.GetCollectionsForPaintKit(paintKitDefIndex, collections))
            {
                issueCount++;
                Platform::Print("InventoryCheck: painted item %llu has no collection mapping (def %u, paint %u)\n",
                    item.id(), item.def_index(), paintKitDefIndex);
            }
        }
        else if (wearAttributes > 0)
        {
            issueCount++;
            Platform::Print("InventoryCheck: item %llu has wear attributes but no paint kit (def %u)\n",
                item.id(), item.def_index());
        }

        if (killEaterAttributes > 0)
        {
            if (scoreTypeAttributes == 0)
            {
                issueCount++;
                Platform::Print("InventoryCheck: StatTrak-like item %llu has kill eater but no score type\n",
                    item.id());
            }
            else if (scoreType == 0 && item.quality() != ItemSchema::QualityStrange)
            {
                issueCount++;
                Platform::Print("InventoryCheck: weapon StatTrak item %llu has quality %u instead of Strange\n",
                    item.id(), item.quality());
            }
        }

        if (casketLowAttributes != casketHighAttributes)
        {
            issueCount++;
            Platform::Print("InventoryCheck: item %llu has incomplete casket reference attributes (low=%d, high=%d)\n",
                item.id(), casketLowAttributes, casketHighAttributes);
        }

        std::optional<uint64_t> storageId = StorageReference(item, m_itemSchema);
        if (storageId.has_value())
        {
            storedItemCountByCasket[*storageId]++;

            auto storageIt = m_items.find(*storageId);
            if (storageIt == m_items.end())
            {
                issueCount++;
                Platform::Print("InventoryCheck: item %llu references missing casket %llu\n",
                    item.id(), *storageId);
            }
            else if (storageIt->second.def_index() != ItemSchema::ItemCasket)
            {
                issueCount++;
                Platform::Print("InventoryCheck: item %llu references non-casket item %llu (def %u)\n",
                    item.id(), *storageId, storageIt->second.def_index());
            }

            if (item.equipped_state_size() > 0)
            {
                issueCount++;
                Platform::Print("InventoryCheck: stored item %llu still has %d equipped state entries\n",
                    item.id(), item.equipped_state_size());
            }
        }
    }

    for (const auto &pair : configuredCasketCountById)
    {
        uint64_t storageId = pair.first;
        uint32_t configuredCount = pair.second;
        uint32_t referencedCount = 0;

        auto referencedIt = storedItemCountByCasket.find(storageId);
        if (referencedIt != storedItemCountByCasket.end())
        {
            referencedCount = referencedIt->second;
        }

        if (configuredCount != referencedCount)
        {
            issueCount++;
            Platform::Print("InventoryCheck: casket %llu count mismatch (attribute=%u, referenced items=%u)\n",
                storageId, configuredCount, referencedCount);
        }
    }

    if (issueCount)
    {
        Platform::Print("InventoryCheck: found %zu potential inventory issue(s)\n", issueCount);
    }
}

bool Inventory::ModifyStorageCounter(CSOEconItem &storage, int delta)
{
    int countIdx = -1, dateIdx = -1;
    
    for (int i = 0; i < storage.attribute_size(); ++i)
    {
        switch (storage.attribute(i).def_index())
        {
        case ItemSchema::AttributeCasketItemsCount:
            countIdx = i;
            break;
        case ItemSchema::AttributeCasketModificationDate:
            dateIdx = i;
            break;
        }
    }
    
    if (countIdx < 0) return false;
    
    auto *countAttr = storage.mutable_attribute(countIdx);
    int32_t current = static_cast<int32_t>(m_itemSchema.AttributeUint32(countAttr));
    int32_t updated = current + delta;
    
    if (updated < 0 || updated > 1000)
        return false;
    
    m_itemSchema.SetAttributeUint32(countAttr, updated);
    
    if (dateIdx >= 0)
    {
        auto *dateAttr = storage.mutable_attribute(dateIdx);
        m_itemSchema.SetAttributeUint32(dateAttr, static_cast<uint32_t>(time(nullptr)));
    }
    
    return true;
}

Inventory::StorageTransaction Inventory::DepositItemToStorage(uint64_t storageId, uint64_t itemId)
{
    StorageTransaction tx{};
    tx.affectedContainerId = storageId;
    
    auto [storage, target] = ResolveStorageItems(storageId, itemId);

    if (storageId == itemId)
    {
        tx.outcome = StorageResult::InvalidContainerType;
        return tx;
    }
    
    if (!storage)
    {
        tx.outcome = StorageResult::ContainerNotFound;
        return tx;
    }
    
    if (!target)
    {
        tx.outcome = StorageResult::ItemNotFound;
        return tx;
    }
    
    if (storage->def_index() != ItemSchema::ItemCasket)
    {
        tx.outcome = StorageResult::InvalidContainerType;
        return tx;
    }

    if (target->def_index() == ItemSchema::ItemCasket)
    {
        tx.outcome = StorageResult::InvalidContainerType;
        return tx;
    }

    if (StorageReference(*target, m_itemSchema).has_value())
    {
        tx.outcome = StorageResult::InternalError;
        return tx;
    }
    
    if (!ModifyStorageCounter(*storage, +1))
    {
        tx.outcome = StorageResult::CapacityExceeded;
        tx.notificationType = k_EGCItemCustomizationNotification_CasketTooFull;
        return tx;
    }
    
    EmbedStorageReference(*target, storageId);
    
    ToSingleObject(tx.itemData, *target);
    ToSingleObject(tx.containerData, *storage);
    tx.notificationType = k_EGCItemCustomizationNotification_CasketAdded;
    tx.outcome = StorageResult::Success;
    
    return tx;
}

Inventory::StorageTransaction Inventory::WithdrawItemFromStorage(uint64_t storageId, uint64_t itemId)
{
    StorageTransaction tx{};
    tx.affectedContainerId = storageId;
    
    auto [storage, target] = ResolveStorageItems(storageId, itemId);
    
    if (!storage)
    {
        tx.outcome = StorageResult::ContainerNotFound;
        return tx;
    }
    
    if (!target)
    {
        tx.outcome = StorageResult::ItemNotFound;
        return tx;
    }
    
    if (storage->def_index() != ItemSchema::ItemCasket)
    {
        tx.outcome = StorageResult::InvalidContainerType;
        return tx;
    }

    std::optional<uint64_t> storedIn = StorageReference(*target, m_itemSchema);
    if (!storedIn.has_value() || *storedIn != storageId)
    {
        tx.outcome = StorageResult::ItemNotFound;
        return tx;
    }
    
    if (!ModifyStorageCounter(*storage, -1))
    {
        tx.outcome = StorageResult::InternalError;
        return tx;
    }
    
    StripStorageReference(*target);
    
    ToSingleObject(tx.itemData, *target);
    ToSingleObject(tx.containerData, *storage);
    tx.notificationType = k_EGCItemCustomizationNotification_CasketRemoved;
    tx.outcome = StorageResult::Success;
    
    return tx;
}

void Inventory::ConsumeToolItem(uint64_t toolId, CMsgSOSingleObject &removalMsg)
{
    if (!GetConfig().DestroyUsedItems())
        return;
        
    auto it = m_items.find(toolId);
    if (it == m_items.end())
        return;
        
    DestroyItem(it, removalMsg);
}

struct CounterSwapWeaponCounters
{
    CSOEconItemAttribute *killEater = nullptr;
    bool usesWeaponKillCounter = false;
};

static CounterSwapWeaponCounters FindCounterSwapWeaponCounters(ItemSchema &schema, CSOEconItem &weapon)
{
    CounterSwapWeaponCounters counters{};

    for (int i = 0; i < weapon.attribute_size(); ++i)
    {
        CSOEconItemAttribute *attribute = weapon.mutable_attribute(i);
        if (attribute->def_index() == ItemSchema::AttributeKillEater)
        {
            counters.killEater = attribute;
        }
        else if (attribute->def_index() == ItemSchema::AttributeKillEaterScoreType
            && schema.AttributeUint32(attribute) == 0)
        {
            counters.usesWeaponKillCounter = true;
        }
    }

    return counters;
}

Inventory::CounterSwapResult Inventory::PerformCounterSwap(uint64_t toolId, uint64_t weaponAId, uint64_t weaponBId)
{
    CounterSwapResult result{};
    result.weaponAId = weaponAId;
    result.weaponBId = weaponBId;

    auto toolIt = m_items.find(toolId);
    if (toolIt == m_items.end())
    {
        Platform::Print("StatTrakSwap: tool item %llu not found for weapons %llu and %llu\n",
            toolId, weaponAId, weaponBId);
        result.status = CounterSwapStatus::ToolMissing;
        return result;
    }

    if (!m_itemSchema.IsStatTrakSwapToolDefIndex(toolIt->second.def_index()))
    {
        Platform::Print("StatTrakSwap: item %llu def %u is not a StatTrak Swap Tool\n",
            toolId, toolIt->second.def_index());
        result.status = CounterSwapStatus::InvalidTool;
        return result;
    }
    
    auto itA = m_items.find(weaponAId);
    auto itB = m_items.find(weaponBId);
    
    bool weaponsExist = (itA != m_items.end()) && (itB != m_items.end());
    if (!weaponsExist)
    {
        result.status = CounterSwapStatus::WeaponMissing;
        return result;
    }
    
    CSOEconItem &weaponA = itA->second;
    CSOEconItem &weaponB = itB->second;

    bool weaponACanSwap = m_itemSchema.CanStatTrakSwapDefIndex(weaponA.def_index());
    bool weaponBCanSwap = m_itemSchema.CanStatTrakSwapDefIndex(weaponB.def_index());
    if (!weaponACanSwap || !weaponBCanSwap)
    {
        Platform::Print("StatTrakSwap: weapon definitions must both support StatTrak Swap (weapon %llu def %u ok=%d, weapon %llu def %u ok=%d)\n",
            weaponAId, weaponA.def_index(), weaponACanSwap ? 1 : 0,
            weaponBId, weaponB.def_index(), weaponBCanSwap ? 1 : 0);
        result.status = CounterSwapStatus::InvalidWeaponState;
        return result;
    }

    if (weaponA.quality() != ItemSchema::QualityStrange
        || weaponB.quality() != ItemSchema::QualityStrange)
    {
        Platform::Print("StatTrakSwap: weapons must both be Strange quality (weapon %llu quality %u, weapon %llu quality %u)\n",
            weaponAId, weaponA.quality(), weaponBId, weaponB.quality());
        result.status = CounterSwapStatus::InvalidWeaponState;
        return result;
    }
    
    CounterSwapWeaponCounters countersA = FindCounterSwapWeaponCounters(m_itemSchema, weaponA);
    CounterSwapWeaponCounters countersB = FindCounterSwapWeaponCounters(m_itemSchema, weaponB);
    CSOEconItemAttribute *attrA = countersA.killEater;
    CSOEconItemAttribute *attrB = countersB.killEater;
    
    bool bothHaveCounters = attrA && attrB;
    if (!bothHaveCounters)
    {
        result.status = CounterSwapStatus::CounterAttributeAbsent;
        return result;
    }

    if (!countersA.usesWeaponKillCounter || !countersB.usesWeaponKillCounter)
    {
        Platform::Print("StatTrakSwap: weapons must both use weapon kill counters (weapon %llu score type ok=%d, weapon %llu score type ok=%d)\n",
            weaponAId, countersA.usesWeaponKillCounter ? 1 : 0,
            weaponBId, countersB.usesWeaponKillCounter ? 1 : 0);
        result.status = CounterSwapStatus::InvalidWeaponState;
        return result;
    }
    
    uint32_t valA = m_itemSchema.AttributeUint32(attrA);
    uint32_t valB = m_itemSchema.AttributeUint32(attrB);
    
    m_itemSchema.SetAttributeUint32(attrA, valB);
    m_itemSchema.SetAttributeUint32(attrB, valA);
    
    ConsumeToolItem(toolId, result.toolRemoval);
    
    ToSingleObject(result.weaponAUpdate, weaponA);
    ToSingleObject(result.weaponBUpdate, weaponB);
    result.status = CounterSwapStatus::Completed;
    
    return result;
}

uint64_t Inventory::PurchaseItem(uint32_t defIndex, std::vector<CMsgSOSingleObject> &update)
{
    if (!m_itemSchema.ItemInfoByDefIndex(defIndex))
    {
        Platform::Print("PurchaseItem: unknown def_index %u\n", defIndex);
        return 0;
    }

    CSOEconItem &item = CreateItem(defIndex, ItemOriginPurchased, UnacknowledgedPurchased);

    CMsgSOSingleObject &single = update.emplace_back();
    ToSingleObject(single, item);

    return item.id();
}

uint64_t Inventory::CreateParameterizedItem(uint32_t defIndex,
    const ParameterizedItemOptions &options,
    CMsgSOSingleObject &update,
    std::string &error)
{
    if (!m_itemSchema.ItemInfoByDefIndex(defIndex))
    {
        error = "unknown defindex";
        return 0;
    }

    if (options.paint && !m_itemSchema.PaintKitInfoByDefIndex(*options.paint))
    {
        error = "unknown paint";
        return 0;
    }

    if (options.music && !m_itemSchema.MusicDefinitionInfoByDefIndex(*options.music))
    {
        error = "unknown music";
        return 0;
    }

    if (options.music && defIndex != ItemSchema::ItemMusicKit)
    {
        error = "music requires defindex 1314";
        return 0;
    }

    if (options.sprayColor
        && (*options.sprayColor < ItemSchema::GraffitiTintMin || *options.sprayColor > ItemSchema::GraffitiTintMax))
    {
        error = "invalid parameter spray_color";
        return 0;
    }

    for (const std::optional<uint32_t> &sticker : options.sticker)
    {
        if (sticker && !m_itemSchema.StickerKitInfoByDefIndex(*sticker))
        {
            error = "unknown sticker";
            return 0;
        }
    }

    CSOEconItem &item = CreateItem(defIndex, ItemOriginPurchased, UnacknowledgedPurchased);

    if (options.level)
    {
        item.set_level(*options.level);
    }

    if (options.customName && !options.customName->empty())
    {
        item.set_custom_name(*options.customName);
    }

    if (options.paint)
    {
        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeTexturePrefab);
        m_itemSchema.SetAttributeUint32(attribute, *options.paint);

        attribute = FindOrAddAttribute(item, ItemSchema::AttributeTextureSeed);
        m_itemSchema.SetAttributeUint32(attribute, options.seed.value_or(0));

        attribute = FindOrAddAttribute(item, ItemSchema::AttributeTextureWear);
        m_itemSchema.SetAttributeFloat(attribute, options.wear.value_or(0.001f));

        if (!options.quality)
        {
            item.set_quality(ItemSchema::QualityUnique);
        }

        if (!options.rarity)
        {
            item.set_rarity(m_itemSchema.GetPaintedRarity(defIndex, *options.paint, item.rarity()));
        }
    }
    else
    {
        if (options.seed)
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeTextureSeed);
            m_itemSchema.SetAttributeUint32(attribute, *options.seed);
        }

        if (options.wear)
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeTextureWear);
            m_itemSchema.SetAttributeFloat(attribute, *options.wear);
        }
    }

    if (options.music)
    {
        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeMusicId);
        m_itemSchema.SetAttributeUint32(attribute, *options.music);

        attribute = FindOrAddAttribute(item, ItemSchema::AttributeKillEater);
        m_itemSchema.SetAttributeUint32(attribute, options.statTrak ? (*options.statTrak == 1 ? 0 : *options.statTrak) : 0);

        attribute = FindOrAddAttribute(item, ItemSchema::AttributeKillEaterScoreType);
        m_itemSchema.SetAttributeUint32(attribute, 1);

        if (!options.quality)
        {
            item.set_quality(options.statTrak ? ItemSchema::QualityStrange : ItemSchema::QualityUnique);
        }

        if (!options.rarity)
        {
            item.set_rarity(ItemSchema::RarityRare);
        }
    }

    if (options.statTrak && !options.music)
    {
        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeKillEater);
        m_itemSchema.SetAttributeUint32(attribute, *options.statTrak == 1 ? 0 : *options.statTrak);

        attribute = FindOrAddAttribute(item, ItemSchema::AttributeKillEaterScoreType);
        m_itemSchema.SetAttributeUint32(attribute, 0);

        if (!options.quality)
        {
            item.set_quality(ItemSchema::QualityStrange);
        }
    }

    if (options.sprayColor)
    {
        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeSprayTintId);
        m_itemSchema.SetAttributeUint32(attribute, *options.sprayColor);
    }

    if (options.sprayRemaining)
    {
        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, ItemSchema::AttributeSpraysRemaining);
        m_itemSchema.SetAttributeUint32(attribute, *options.sprayRemaining);
    }

    auto setTournamentAttribute = [&](const std::optional<uint32_t> &value, uint32_t attributeDefIndex)
    {
        if (!value)
        {
            return;
        }

        CSOEconItemAttribute *attribute = FindOrAddAttribute(item, attributeDefIndex);
        m_itemSchema.SetAttributeUint32(attribute, *value);
    };

    setTournamentAttribute(options.tournament.eventId, ItemSchema::AttributeTournamentEventId);
    setTournamentAttribute(options.tournament.stageId, ItemSchema::AttributeTournamentEventStageId);
    setTournamentAttribute(options.tournament.team0Id, ItemSchema::AttributeTournamentTeam0Id);
    setTournamentAttribute(options.tournament.team1Id, ItemSchema::AttributeTournamentTeam1Id);
    setTournamentAttribute(options.tournament.mvpAccountId, ItemSchema::AttributeTournamentMvpAccountId);

    for (size_t i = 0; i < options.sticker.size(); i++)
    {
        if (options.sticker[i])
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, StickerIdAttribute(i));
            m_itemSchema.SetAttributeUint32(attribute, *options.sticker[i]);

            attribute = FindOrAddAttribute(item, StickerWearAttribute(i));
            m_itemSchema.SetAttributeFloat(attribute, options.stickerWear[i].value_or(0.0f));
        }
        else if (options.stickerWear[i])
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, StickerWearAttribute(i));
            m_itemSchema.SetAttributeFloat(attribute, *options.stickerWear[i]);
        }

        if (options.stickerScale[i])
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, StickerScaleAttribute(i));
            m_itemSchema.SetAttributeFloat(attribute, *options.stickerScale[i]);
        }

        if (options.stickerRotation[i])
        {
            CSOEconItemAttribute *attribute = FindOrAddAttribute(item, StickerRotationAttribute(i));
            m_itemSchema.SetAttributeFloat(attribute, *options.stickerRotation[i]);
        }
    }

    if (options.quality)
    {
        item.set_quality(*options.quality);
    }

    if (options.rarity)
    {
        item.set_rarity(*options.rarity);
    }

    ToSingleObject(update, item);
    return item.id();
}

bool Inventory::UnequipItem(uint64_t itemId, CMsgSOMultipleObjects &update)
{
    uint32_t defIndex, paintKitIndex;
    if (IsDefaultItemId(itemId, defIndex, paintKitIndex))
    {
        // not supported
        assert(false);
        return false;
    }

    auto it = m_items.find(itemId);
    if (it == m_items.end())
    {
        assert(false);
        return false;
    }

    CSOEconItem &item = it->second;
    item.clear_equipped_state();

    AddToMultipleObjects(update, item);

    return true;
}

// this goes through everything on purpose
void Inventory::UnequipItem(uint32_t classId, uint32_t slotId, CMsgSOMultipleObjects &update)
{
    // check non default items first
    for (auto &pair : m_items)
    {
        CSOEconItem &item = pair.second;

        bool modified = false;

        for (auto it = item.mutable_equipped_state()->begin(); it != item.mutable_equipped_state()->end();)
        {
            if (it->new_class() == classId && it->new_slot() == slotId)
            {
                Platform::Print("Unequip %llu class %d slot %d\n", pair.first, classId, slotId);

                it = item.mutable_equipped_state()->erase(it);
                modified = true;
            }
            else
            {
                it++;
            }
        }

        if (modified)
        {
            AddToMultipleObjects(update, item);
        }
    }

    // check default equips
    for (auto it = m_defaultEquips.begin(); it != m_defaultEquips.end();)
    {
        if (it->class_id() == classId && it->slot_id() == slotId)
        {
            Platform::Print("Unequip %u class %d slot %d\n", it->item_definition(), classId, slotId);

            // mikkotodo is this correct???
            // mikkotodo rpobably not correct.. i gess we don't even have to do this
            // because the new equip overrides the old one
            // but we can't just remove it either because "update" would get fucked
            it->set_item_definition(0);
            AddToMultipleObjects(update, *it);

            it = m_defaultEquips.erase(it);
        }
        else
        {
            it++;
        }
    }
}

void Inventory::DestroyItem(ItemMap::iterator iterator, CMsgSOSingleObject &message)
{
    CSOEconItem item;
    item.set_id(iterator->second.id());

    ToSingleObject(message, item);

    m_items.erase(iterator);
}

bool Inventory::TradeUp(const std::vector<uint64_t> &inputItemIds,
    std::vector<CMsgSOSingleObject> &destroyItems,
    CMsgSOSingleObject &newItem,
    CMsgGCItemCustomizationNotification &notification,
    CSOEconItem **outCraftedItem)
{
    if (inputItemIds.size() != 10)
    {
        Platform::Print("Trade-up requires exactly 10 items, got %zu\n", inputItemIds.size());
        return false;
    }

    std::vector<ItemMap::iterator> inputItems;
    inputItems.reserve(10);

    uint32_t inputRarity = 0;
    bool statTrakSet = false;
    bool hasStatTrak = false;
    float totalWear = 0.0f;
    int wearCount = 0;

    std::map<std::string, int> collectionCounts;
    std::unordered_set<uint64_t> uniqueInputItemIds;
    uniqueInputItemIds.reserve(inputItemIds.size());

    struct TradeUpItemDebug
    {
        uint64_t itemId{};
        uint32_t defIndex{};
        uint32_t paintKitDefIndex{};
        uint32_t storedRarity{};
        uint32_t paintedRarity{};
        uint32_t quality{};
        std::string collectionId;
    };

    auto printItemDebug = [this](const char *prefix, const TradeUpItemDebug &debug)
    {
        Platform::Print("%s item %llu: def %u, paint %u, stored rarity %u, painted rarity %u, quality %u, collection %s (%s)\n",
            prefix,
            debug.itemId,
            debug.defIndex,
            debug.paintKitDefIndex,
            debug.storedRarity,
            debug.paintedRarity,
            debug.quality,
            debug.collectionId.c_str(),
            GetCollectionName(m_itemSchema, debug.collectionId).c_str());
    };

    for (uint64_t itemId : inputItemIds)
    {
        if (!uniqueInputItemIds.insert(itemId).second)
        {
            Platform::Print("Trade-up item %llu was submitted more than once\n", itemId);
            return false;
        }

        auto it = m_items.find(itemId);
        if (it == m_items.end())
        {
            Platform::Print("Trade-up item %llu not found\n", itemId);
            return false;
        }

        const CSOEconItem &item = it->second;
        inputItems.push_back(it);

        TradeUpItemDebug debug;
        debug.itemId = itemId;
        debug.defIndex = item.def_index();
        debug.storedRarity = item.rarity();
        debug.quality = item.quality();

        uint32_t paintKitDefIndex = 0;
        if (!GetItemPaintKitDefIndex(item, m_itemSchema, paintKitDefIndex))
        {
            Platform::Print("Trade-up item %llu has no paint kit (def %u, stored rarity %u, quality %u)\n",
                itemId, item.def_index(), item.rarity(), item.quality());
            return false;
        }
        debug.paintKitDefIndex = paintKitDefIndex;

        uint32_t rarity = m_itemSchema.GetPaintedRarity(item.def_index(), paintKitDefIndex, item.rarity());
        debug.paintedRarity = rarity;
        if (rarity < ItemSchema::RarityCommon || rarity > ItemSchema::RarityLegendary)
        {
            printItemDebug("Trade-up item has invalid rarity", debug);
            return false;
        }

        if (inputRarity == 0)
        {
            inputRarity = rarity;
        }
        else if (rarity != inputRarity)
        {
            Platform::Print("Trade-up items must all be same painted rarity (expected %u, got %u)\n",
                inputRarity, rarity);
            printItemDebug("Mismatched trade-up rarity", debug);
            return false;
        }

        std::vector<std::string> collections;
        if (!m_itemSchema.GetCollectionsForPaintedItem(item.def_index(), paintKitDefIndex, collections))
        {
            if (!m_itemSchema.GetCollectionsForPaintKit(paintKitDefIndex, collections))
            {
                Platform::Print("Trade-up item %llu has no collection mapping (def %u, paint %u, stored rarity %u, painted rarity %u, quality %u)\n",
                    itemId, item.def_index(), paintKitDefIndex, item.rarity(), rarity, item.quality());
                return false;
            }
        }

        std::sort(collections.begin(), collections.end());
        const std::string &collectionId = collections.front();
        debug.collectionId = collectionId;
        collectionCounts[collectionId]++;

        printItemDebug("Trade-up input", debug);

        bool hasWear = false;
        bool hasKillEater = false;
        bool hasWeaponKillEaterScoreType = false;
        for (const CSOEconItemAttribute &attr : item.attribute())
        {
            if (attr.def_index() == ItemSchema::AttributeKillEater)
            {
                hasKillEater = true;
            }
            else if (attr.def_index() == ItemSchema::AttributeKillEaterScoreType)
            {
                hasWeaponKillEaterScoreType = m_itemSchema.AttributeUint32(&attr) == 0;
            }
            else if (attr.def_index() == ItemSchema::AttributeTextureWear)
            {
                hasWear = true;
                totalWear += m_itemSchema.AttributeFloat(&attr);
                wearCount++;
            }
        }

        if (!hasWear)
        {
            Platform::Print("Trade-up item %llu has no wear attribute; cannot calculate contract output float\n",
                itemId);
            printItemDebug("Missing trade-up wear", debug);
            return false;
        }

        bool itemNormalTradeUp = (item.quality() == ItemSchema::QualityUnique
            || item.quality() == ItemSchema::QualityTournament)
            && !hasKillEater;
        bool itemStatTrak = item.quality() == ItemSchema::QualityStrange
            && hasKillEater
            && hasWeaponKillEaterScoreType;
        if (!itemNormalTradeUp && !itemStatTrak)
        {
            Platform::Print("Trade-up item %llu has unsupported quality/state (kill eater=%d, weapon score type=%d)\n",
                itemId, hasKillEater ? 1 : 0, hasWeaponKillEaterScoreType ? 1 : 0);
            printItemDebug("Unsupported trade-up item", debug);
            return false;
        }

        if (!statTrakSet)
        {
            hasStatTrak = itemStatTrak;
            statTrakSet = true;
        }
        else if (itemStatTrak != hasStatTrak)
        {
            Platform::Print("Trade-up items must all be StatTrak or all non-StatTrak (expected stattrak=%d, got stattrak=%d)\n",
                hasStatTrak ? 1 : 0, itemStatTrak ? 1 : 0);
            printItemDebug("Mismatched StatTrak state", debug);
            return false;
        }
    }

    float avgWear = 0.15f;
    if (wearCount > 0)
    {
        avgWear = totalWear / wearCount;
        if (avgWear < 0.0f) avgWear = 0.0f;
        if (avgWear > 1.0f) avgWear = 1.0f;
    }

    uint32_t outputRarity = inputRarity + 1;
    if (outputRarity > ItemSchema::RarityAncient)
    {
        Platform::Print("Cannot trade up items of rarity %u (max output is ancient)\n", inputRarity);
        return false;
    }

    std::vector<std::string> weightedCollections;
    for (const auto &pair : collectionCounts)
    {
        const std::string &collection = pair.first;
        std::vector<const LootListItem *> candidates;
        if (!m_itemSchema.GetTradeUpCandidates(collection, outputRarity, candidates))
        {
            Platform::Print("%s Collection has no trade-up candidates at output rarity %u; rejecting contract with %d input item(s) from this collection\n",
                GetCollectionName(m_itemSchema, collection).c_str(), outputRarity, pair.second);
            return false;
        }

        int count = pair.second;
        for (int i = 0; i < count; i++)
        {
            weightedCollections.push_back(collection);
        }

        float percentage = (float)count / 10.0f * 100.0f;
        Platform::Print("%s Collection: %.1f%%\n", GetCollectionName(m_itemSchema, collection).c_str(), percentage);
    }

    if (weightedCollections.empty())
    {
        Platform::Print("No valid trade-up collections with rarity %u\n", outputRarity);
        return false;
    }

    uint32_t roll = m_random.Integer<uint32_t>(0, static_cast<uint32_t>(weightedCollections.size() - 1));
    const std::string &selectedCollection = weightedCollections[roll];
    Platform::Print("RNG roll: %u, selected collection %s (%s)\n", roll, selectedCollection.c_str(),
        GetCollectionName(m_itemSchema, selectedCollection).c_str());

    std::vector<const LootListItem *> outputCandidates;
    if (!m_itemSchema.GetTradeUpCandidates(selectedCollection, outputRarity, outputCandidates))
    {
        Platform::Print("No trade-up candidates for collection %s at rarity %u\n",
            selectedCollection.c_str(), outputRarity);
        return false;
    }

    std::vector<const LootListItem *> validCandidates;
    validCandidates.reserve(outputCandidates.size());

    for (const LootListItem *candidate : outputCandidates)
    {
        if (!candidate || !candidate->paintKitInfo)
        {
            continue;
        }

        float minWear = candidate->paintKitInfo->m_minFloat;
        float maxWear = candidate->paintKitInfo->m_maxFloat;
        float mappedWear = minWear + avgWear * (maxWear - minWear);

        if (mappedWear < minWear || mappedWear > maxWear)
        {
            continue;
        }

        validCandidates.push_back(candidate);
    }

    if (validCandidates.empty())
    {
        Platform::Print("No valid trade-up candidates after float filtering for collection %s\n",
            selectedCollection.c_str());
        return false;
    }

    uint32_t candidateIndex = m_random.Integer<uint32_t>(0, static_cast<uint32_t>(validCandidates.size() - 1));
    const LootListItem *selectedCandidate = validCandidates[candidateIndex];

    CSOEconItem &outputItem = AllocateItem(0);

    outputItem.set_def_index(selectedCandidate->itemInfo->m_defIndex);
    outputItem.set_inventory(InventoryUnacknowledged(UnacknowledgedRecycling));
    outputItem.set_quantity(1);
    outputItem.set_level(1);
    outputItem.set_origin(ItemOriginCrate);
    outputItem.set_rarity(outputRarity);
    outputItem.set_quality(hasStatTrak ? ItemSchema::QualityStrange : ItemSchema::QualityUnique);
    outputItem.set_flags(0);
    outputItem.set_in_use(false);

    uint32_t paintKitId = selectedCandidate->paintKitInfo->m_defIndex;

    CSOEconItemAttribute *paintAttr = outputItem.add_attribute();
    paintAttr->set_def_index(ItemSchema::AttributeTexturePrefab);
    m_itemSchema.SetAttributeUint32(paintAttr, paintKitId);

    CSOEconItemAttribute *seedAttr = outputItem.add_attribute();
    seedAttr->set_def_index(ItemSchema::AttributeTextureSeed);
    m_itemSchema.SetAttributeUint32(seedAttr, m_random.Integer<uint32_t>(0, 1000));

    float outputWear = avgWear;
    if (selectedCandidate->paintKitInfo)
    {
        float minWear = selectedCandidate->paintKitInfo->m_minFloat;
        float maxWear = selectedCandidate->paintKitInfo->m_maxFloat;
        outputWear = minWear + avgWear * (maxWear - minWear);
    }
    CSOEconItemAttribute *wearAttr = outputItem.add_attribute();
    wearAttr->set_def_index(ItemSchema::AttributeTextureWear);
    m_itemSchema.SetAttributeFloat(wearAttr, outputWear);

    if (hasStatTrak)
    {
        CSOEconItemAttribute *killAttr = outputItem.add_attribute();
        killAttr->set_def_index(ItemSchema::AttributeKillEater);
        m_itemSchema.SetAttributeUint32(killAttr, 0);

        CSOEconItemAttribute *scoreTypeAttr = outputItem.add_attribute();
        scoreTypeAttr->set_def_index(ItemSchema::AttributeKillEaterScoreType);
        m_itemSchema.SetAttributeUint32(scoreTypeAttr, 0);
    }

    destroyItems.reserve(inputItems.size());
    for (auto it : inputItems)
    {
        CMsgSOSingleObject &destroy = destroyItems.emplace_back();
        DestroyItem(it, destroy);
    }

    ToSingleObject(newItem, outputItem);

    notification.add_item_id(outputItem.id());
    notification.set_request(k_EGCItemCustomizationNotification_UnlockCrate);

    if (outCraftedItem)
    {
        *outCraftedItem = &outputItem;
    }

    Platform::Print("Trade-up complete: created item %llu from collection %s (%s), def %u, rarity %u, wear %.4f, stattrak=%d\n",
        outputItem.id(), selectedCollection.c_str(), GetCollectionName(m_itemSchema, selectedCollection).c_str(),
        outputItem.def_index(), selectedCandidate->rarity, outputWear, hasStatTrak ? 1 : 0);

    return true;
}
