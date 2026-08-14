#include "stdafx.h"
#include "gc_client.h"
#include "gc_message.h"
#include "keyvalue.h"
#include "test_filesystem.h"

#include <cstring>
#include <cstdio>

namespace Platform
{

void Print(const char *, ...)
{
}

bool UpdateGraffitiKey(std::string_view, const void *, const void *, size_t)
{
    return true;
}

}

template<typename T>
static bool ValueAt(const uint8_t *data, size_t offset, T expected)
{
    T actual{};
    std::memcpy(&actual, data + offset, sizeof(actual));
    return actual == expected;
}

static bool ExtendedCraftResponseSerialization()
{
    constexpr int16_t responseIndex = 12;
    constexpr uint64_t craftedItemId = 0x1122334455667788ull;

    GCMessageWrite message = BuildCraftResponseMessage(
        responseIndex,
        k_EGCMsgResponseOK,
        craftedItemId);

    const auto *data = static_cast<const uint8_t *>(message.Data());
    size_t offset = 0;

    bool valid = ValueAt(data, offset, static_cast<uint32_t>(k_EMsgGCCraftResponse));
    offset += sizeof(uint32_t);
    valid &= ValueAt(data, offset, uint32_t{ 0 });
    offset += sizeof(uint32_t);
    valid &= ValueAt(data, offset, uint64_t{ 0 });
    offset += sizeof(uint64_t);
    valid &= ValueAt(data, offset, uint16_t{ 1 });
    offset += sizeof(uint16_t);
    valid &= ValueAt(data, offset, JobIdInvalid);
    offset += sizeof(uint64_t);
    valid &= ValueAt(data, offset, JobIdInvalid);
    offset += sizeof(uint64_t);
    valid &= ValueAt(data, offset, responseIndex);
    offset += sizeof(int16_t);
    valid &= ValueAt(data, offset, static_cast<uint32_t>(k_EGCMsgResponseOK));
    offset += sizeof(uint32_t);
    valid &= ValueAt(data, offset, uint16_t{ 1 });
    offset += sizeof(uint16_t);
    valid &= ValueAt(data, offset, craftedItemId);
    offset += sizeof(uint64_t);

    return valid && offset == message.Size();
}

static bool TruncatedCraftRequestGetsInvalidResponse()
{
    constexpr int16_t recipe = -3;

    GCMessageWrite request{ k_EMsgGCCraft };
    request.WriteUint16(static_cast<uint16_t>(recipe));

    ClientGC gc{ 76561197960265729ull };
    gc.PostToGC(GCEvent::Message, k_EMsgGCCraft, request.Data(), request.Size());

    std::vector<EventData> events;
    EventData responseEvent;
    bool foundResponse = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (!foundResponse && std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (EventData &event : events)
        {
            if (event.type == static_cast<int>(HostEvent::Message)
                && event.id == k_EMsgGCCraftResponse)
            {
                responseEvent = std::move(event);
                foundResponse = true;
                break;
            }
        }

        events.clear();
        if (!foundResponse)
        {
            std::this_thread::yield();
        }
    }

    if (!foundResponse)
    {
        return false;
    }

    const auto *data = responseEvent.buffer.data();
    constexpr size_t responseBodyOffset = sizeof(uint32_t) + sizeof(uint32_t)
        + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint64_t);

    return ValueAt(data, responseBodyOffset, recipe)
        && ValueAt(data, responseBodyOffset + sizeof(int16_t),
            static_cast<uint32_t>(k_EGCMsgResponseInvalid))
        && ValueAt(data, responseBodyOffset + sizeof(int16_t) + sizeof(uint32_t), uint16_t{ 0 })
        && responseEvent.buffer.size()
            == responseBodyOffset + sizeof(int16_t) + sizeof(uint32_t) + sizeof(uint16_t);
}

static bool BasicStructHeaderSerializationIsUnchanged()
{
    constexpr uint32_t messageType = 1;
    GCMessageWrite message{ messageType };

    const auto *data = static_cast<const uint8_t *>(message.Data());
    size_t offset = 0;

    bool valid = ValueAt(data, offset, messageType);
    offset += sizeof(uint32_t);
    valid &= ValueAt(data, offset, uint32_t{ 0 });
    offset += sizeof(uint32_t);
    valid &= ValueAt(data, offset, uint64_t{ 0 });
    offset += sizeof(uint64_t);
    valid &= ValueAt(data, offset, uint16_t{ 0 });
    offset += sizeof(uint16_t);

    return valid && offset == message.Size();
}

static bool WaitForHostMessage(ClientGC &gc, uint32_t type, EventData &result)
{
    std::vector<EventData> events;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (EventData &event : events)
        {
            if (event.type == static_cast<int>(HostEvent::Message)
                && (event.id & ~ProtobufMask) == type)
            {
                result = std::move(event);
                return true;
            }
        }

        events.clear();
        std::this_thread::yield();
    }

    return false;
}

template<typename T>
static bool ParseHostProtobuf(const EventData &event, T &message)
{
    GCMessageRead messageRead{ 0, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()) };
    return messageRead.IsValid() && messageRead.IsProtobuf() && messageRead.ReadProtobuf(message);
}

template<typename T>
static void SendGCProtobuf(ClientGC &gc, uint32_t type, const T &message)
{
    GCMessageWrite messageWrite{ type, message };
    gc.PostToGC(GCEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}

static bool InventoryPersistenceProtectsFiles()
{
    constexpr const char *InventoryDirectory = "csgo_gc";
    constexpr const char *InventoryPath = "csgo_gc/inventory.txt";
    constexpr std::string_view MalformedInventory{ "\"items\"\n{\n\"2\"\n{\n" };

    if (!TestFilesystem::MakeDirectory(InventoryDirectory))
    {
        return false;
    }

    FILE *f = fopen(InventoryPath, "wb");
    if (!f)
    {
        return false;
    }

    bool wroteFile = fwrite(MalformedInventory.data(), 1, MalformedInventory.size(), f)
        == MalformedInventory.size();
    wroteFile &= fclose(f) == 0;
    if (!wroteFile)
    {
        return false;
    }

    {
        Inventory inventory{ 76561197960265729ull };
    }

    bool preserved = LoadFile(InventoryPath) == MalformedInventory;
    TestFilesystem::RemoveFile(InventoryPath);

    {
        Inventory inventory{ 76561197960265729ull };
    }

    KeyValue savedInventory{ "inventory" };
    bool validEmptyInventory = savedInventory.ParseFromFile(InventoryPath)
        && savedInventory.GetNumber<int>("format_version") == 1;

    TestFilesystem::RemoveFile(InventoryPath);
    TestFilesystem::RemoveDirectory(InventoryDirectory);
    return preserved && validEmptyInventory;
}

static int EquippedSlotForClass(const CSOEconItem &item, uint32_t classId)
{
    int slot = -1;
    for (const CSOEconItemEquipped &equipped : item.equipped_state())
    {
        if (equipped.new_class() == classId)
        {
            if (slot != -1)
            {
                return -2;
            }
            slot = static_cast<int>(equipped.new_slot());
        }
    }
    return slot;
}

static bool UpdateContainsDefaultEquip(const CMsgSOMultipleObjects &update,
    uint32_t defIndex, uint32_t classId, uint32_t slotId)
{
    for (const CMsgSOMultipleObjects_SingleObject &object : update.objects_modified())
    {
        if (object.type_id() != SOTypeDefaultEquippedDefinitionInstanceClient)
        {
            continue;
        }

        CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
        if (defaultEquip.ParseFromString(object.object_data())
            && defaultEquip.item_definition() == defIndex
            && defaultEquip.class_id() == classId
            && defaultEquip.slot_id() == slotId)
        {
            return true;
        }
    }

    return false;
}

static bool WriteLoadoutFixture()
{
    if (!TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &items = inventory.AddSubkey("items");

    auto addItem = [&](const char *highItemId, uint32_t defIndex, uint32_t class2Slot,
                       bool equipForClass3)
    {
        KeyValue &item = items.AddSubkey(highItemId);
        item.AddNumber("def_index", defIndex);
        KeyValue &equippedState = item.AddSubkey("equipped_state");
        equippedState.AddNumber("2", class2Slot);
        if (equipForClass3)
        {
            equippedState.AddNumber("3", class2Slot);
        }
    };

    addItem("1", 7, 1, true);
    addItem("2", 8, 2, false);
    addItem("3", 10, 3, false);

    KeyValue &defaultEquips = inventory.AddSubkey("default_equips");
    KeyValue &defaultEquip = defaultEquips.AddSubkey("9");
    defaultEquip.AddNumber("class_id", 2);
    defaultEquip.AddNumber("slot_id", 4);
    return inventory.WriteToFile("csgo_gc/inventory.txt");
}

static bool LoadoutStateTransitionsPreserveClassesAndSwapSlots()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    constexpr uint64_t Item1 = (uint64_t{ 1 } << 32) | (SteamId & UINT32_MAX);
    constexpr uint64_t Item2 = (uint64_t{ 2 } << 32) | (SteamId & UINT32_MAX);
    constexpr uint64_t Item3 = (uint64_t{ 3 } << 32) | (SteamId & UINT32_MAX);
    constexpr uint64_t DefaultItem = ItemIdDefaultItemMask | 9;

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    if (!WriteLoadoutFixture())
    {
        return false;
    }

    bool valid = true;
    {
        Inventory inventory{ SteamId };
        uint64_t version = inventory.Version();
        valid &= version != 0;

        CMsgSOMultipleObjects unequip;
        valid &= inventory.EquipItem(Item1, 2, 0xffff, false, unequip);
        valid &= unequip.version() == version + 1 && inventory.Version() == unequip.version();
        version = inventory.Version();
        const CSOEconItem *item1 = inventory.GetItem(Item1);
        valid &= item1 && EquippedSlotForClass(*item1, 2) == -1
            && EquippedSlotForClass(*item1, 3) == 1;

        CMsgSOMultipleObjects move;
        valid &= inventory.EquipItem(Item2, 2, 5, false, move);
        valid &= move.version() == version + 1 && inventory.Version() == move.version();
        version = inventory.Version();
        const CSOEconItem *item2 = inventory.GetItem(Item2);
        valid &= item2 && EquippedSlotForClass(*item2, 2) == 5;

        CMsgSOMultipleObjects uniqueSwap;
        valid &= inventory.EquipItem(Item2, 2, 3, true, uniqueSwap);
        valid &= uniqueSwap.version() == version + 1
            && inventory.Version() == uniqueSwap.version()
            && uniqueSwap.objects_modified_size() >= 2;
        version = inventory.Version();
        item2 = inventory.GetItem(Item2);
        const CSOEconItem *item3 = inventory.GetItem(Item3);
        valid &= item2 && item3
            && EquippedSlotForClass(*item2, 2) == 3
            && EquippedSlotForClass(*item3, 2) == -1;

        CMsgSOMultipleObjects defaultSwap;
        valid &= inventory.EquipItem(DefaultItem, 2, 3, true, defaultSwap);
        valid &= defaultSwap.version() == version + 1
            && inventory.Version() == defaultSwap.version()
            && UpdateContainsDefaultEquip(defaultSwap, 9, 2, 3);
        item1 = inventory.GetItem(Item1);
        item2 = inventory.GetItem(Item2);
        valid &= item1 && item2
            && EquippedSlotForClass(*item1, 3) == 1
            && EquippedSlotForClass(*item2, 2) == 4;
    }

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveDirectory("csgo_gc");
    return valid;
}

static bool SOCacheVersionNegotiationAndRefresh()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    if (!TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };

        CMsgClientHello hello;
        hello.set_version(1);
        GCMessageWrite helloMessage{ k_EMsgGCClientHello, hello };
        gc.PostToGC(GCEvent::Message, helloMessage.TypeMasked(), helloMessage.Data(), helloMessage.Size());

        EventData event;
        CMsgClientWelcome welcome;
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && welcome.outofdate_subscribed_caches_size() == 1
            && welcome.uptodate_subscribed_caches_size() == 0;

        uint64_t version = 0;
        if (welcome.outofdate_subscribed_caches_size() == 1)
        {
            const CMsgSOCacheSubscribed &subscription = welcome.outofdate_subscribed_caches(0);
            version = subscription.version();
            valid &= version != 0
                && subscription.has_owner_soid()
                && subscription.owner_soid().type() == SoIdTypeSteamId
                && subscription.owner_soid().id() == SteamId;

            bool foundAccount = false;
            for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
            {
                if (type.type_id() == SOTypeGameAccountClient && type.object_data_size() == 1)
                {
                    CSOEconGameAccountClient account;
                    foundAccount = account.ParseFromString(type.object_data(0));
                    valid &= foundAccount && !account.has_elevated_timestamp();
                }
            }
            valid &= foundAccount;
        }

        CMsgClientHello currentHello;
        CMsgSOCacheHaveVersion *haveVersion = currentHello.add_socache_have_versions();
        haveVersion->mutable_soid()->set_type(SoIdTypeSteamId);
        haveVersion->mutable_soid()->set_id(SteamId);
        haveVersion->set_version(version);
        GCMessageWrite currentHelloMessage{ k_EMsgGCClientHello, currentHello };
        gc.PostToGC(GCEvent::Message, currentHelloMessage.TypeMasked(), currentHelloMessage.Data(),
            currentHelloMessage.Size());

        event = {};
        welcome.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && welcome.outofdate_subscribed_caches_size() == 0
            && welcome.uptodate_subscribed_caches_size() == 1;
        if (welcome.uptodate_subscribed_caches_size() == 1)
        {
            const CMsgSOCacheSubscriptionCheck &check = welcome.uptodate_subscribed_caches(0);
            valid &= check.version() == version
                && check.has_owner_soid()
                && check.owner_soid().type() == SoIdTypeSteamId
                && check.owner_soid().id() == SteamId;
        }

        CMsgSOCacheSubscriptionRefresh refresh;
        refresh.mutable_owner_soid()->set_type(SoIdTypeSteamId);
        refresh.mutable_owner_soid()->set_id(SteamId);
        GCMessageWrite refreshMessage{ k_ESOMsg_CacheSubscriptionRefresh, refresh };
        gc.PostToGC(GCEvent::Message, refreshMessage.TypeMasked(), refreshMessage.Data(),
            refreshMessage.Size());

        event = {};
        CMsgSOCacheSubscribed refreshed;
        valid &= WaitForHostMessage(gc, k_ESOMsg_CacheSubscribed, event)
            && ParseHostProtobuf(event, refreshed)
            && refreshed.version() == version
            && refreshed.owner_soid().id() == SteamId;
    }

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveDirectory("csgo_gc");
    return valid;
}

static bool WriteStoreFixtures()
{
    if (!TestFilesystem::MakeDirectory("csgo")
        || !TestFilesystem::MakeDirectory("csgo/scripts")
        || !TestFilesystem::MakeDirectory("csgo/scripts/items")
        || !TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    KeyValue schema{ "root" };
    KeyValue &itemsGame = schema.AddSubkey("items_game");
    KeyValue &items = itemsGame.AddSubkey("items");
    items.AddSubkey("7").AddString("name", "weapon_ak47");

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    unusualLootLists.AddSubkey("empty");

    KeyValue priceSheet{ "price_sheet" };
    KeyValue &store = priceSheet.AddSubkey("store");
    store.AddNumber("featured_item_index", 7);

    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && priceSheet.WriteToFile("csgo_gc/price_sheet.txt");
}

static void RemoveStoreFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/price_sheet.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool StorePurchasesFinalizeTransactionally()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveStoreFixtures();
    if (!WriteStoreFixtures())
    {
        RemoveStoreFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };

        CMsgClientHello hello;
        SendGCProtobuf(gc, k_EMsgGCClientHello, hello);
        EventData event;
        CMsgClientWelcome welcome;
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && welcome.outofdate_subscribed_caches_size() == 1;

        uint64_t initialCacheVersion = welcome.outofdate_subscribed_caches_size() == 1
            ? welcome.outofdate_subscribed_caches(0).version() : 0;
        CMsgGCCStrike15_v2_MatchmakingGC2ClientHello matchmakingHello;
        valid &= matchmakingHello.ParseFromString(welcome.game_data2());
        uint32_t priceSheetVersion = matchmakingHello.global_stats().pricesheet_version();

        CMsgStoreGetUserData storeData;
        storeData.set_price_sheet_version(0);
        SendGCProtobuf(gc, k_EMsgGCStoreGetUserData, storeData);
        event = {};
        CMsgStoreGetUserDataResponse storeDataResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStoreGetUserDataResponse, event)
            && ParseHostProtobuf(event, storeDataResponse)
            && storeDataResponse.result() == 1
            && storeDataResponse.price_sheet_version() == priceSheetVersion
            && !storeDataResponse.price_sheet().empty();

        CMsgGCStorePurchaseInit purchaseInit;
        CGCStorePurchaseInit_LineItem *lineItem = purchaseInit.add_line_items();
        lineItem->set_item_def_id(7);
        lineItem->set_quantity(2);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchaseInit);
        event = {};
        CMsgGCStorePurchaseInitResponse initResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() == 1
            && initResponse.txn_id() != 0
            && initResponse.item_ids_size() == 0;

        CMsgClientHello currentHello;
        CMsgSOCacheHaveVersion *haveVersion = currentHello.add_socache_have_versions();
        haveVersion->mutable_soid()->set_type(SoIdTypeSteamId);
        haveVersion->mutable_soid()->set_id(SteamId);
        haveVersion->set_version(initialCacheVersion);
        SendGCProtobuf(gc, k_EMsgGCClientHello, currentHello);
        event = {};
        welcome.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && welcome.uptodate_subscribed_caches_size() == 1;

        CMsgGCStorePurchaseFinalize wrongFinalize;
        wrongFinalize.set_txn_id(initResponse.txn_id() + 1);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseFinalize, wrongFinalize);
        event = {};
        CMsgGCStorePurchaseFinalizeResponse finalizeResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseFinalizeResponse, event)
            && ParseHostProtobuf(event, finalizeResponse)
            && finalizeResponse.result() != 1
            && finalizeResponse.item_ids_size() == 0;

        CMsgGCStorePurchaseFinalize finalize;
        finalize.set_txn_id(initResponse.txn_id());
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseFinalize, finalize);
        event = {};
        finalizeResponse.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseFinalizeResponse, event)
            && ParseHostProtobuf(event, finalizeResponse)
            && finalizeResponse.result() == 1
            && finalizeResponse.item_ids_size() == 2;

        SendGCProtobuf(gc, k_EMsgGCClientHello, currentHello);
        event = {};
        welcome.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && welcome.outofdate_subscribed_caches_size() == 1;
        if (welcome.outofdate_subscribed_caches_size() == 1)
        {
            const CMsgSOCacheSubscribed &subscription = welcome.outofdate_subscribed_caches(0);
            valid &= subscription.version() != initialCacheVersion;

            std::unordered_set<uint64_t> snapshotItemIds;
            for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
            {
                if (type.type_id() != SOTypeItem)
                {
                    continue;
                }
                for (const std::string &objectData : type.object_data())
                {
                    CSOEconItem item;
                    if (item.ParseFromString(objectData))
                    {
                        snapshotItemIds.insert(item.id());
                    }
                }
            }

            valid &= snapshotItemIds.size() == 2;
            for (uint64_t itemId : finalizeResponse.item_ids())
            {
                valid &= snapshotItemIds.contains(itemId);
            }
        }

        SendGCProtobuf(gc, k_EMsgGCStorePurchaseFinalize, finalize);
        event = {};
        finalizeResponse.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseFinalizeResponse, event)
            && ParseHostProtobuf(event, finalizeResponse)
            && finalizeResponse.result() != 1;
    }

    RemoveStoreFixtures();
    return valid;
}

int main()
{
    return ExtendedCraftResponseSerialization()
        && TruncatedCraftRequestGetsInvalidResponse()
        && BasicStructHeaderSerializationIsUnchanged()
        && InventoryPersistenceProtectsFiles()
        && LoadoutStateTransitionsPreserveClassesAndSwapSlots()
        && SOCacheVersionNegotiationAndRefresh()
        && StorePurchasesFinalizeTransactionally() ? 0 : 1;
}
