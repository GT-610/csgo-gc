#include "stdafx.h"
#include "case_opening.h"
#include "gc_client.h"
#include "gc_message.h"
#include "game_schema_patch.h"
#include "keyvalue.h"
#include "networking_client.h"
#include "networking_server.h"
#include "panorama_hook.h"
#include "star_shop.h"
#include "bot_skins.h"
#include "viewmodel_range.h"
#include "test_filesystem.h"

#include <cmath>
#include <cstring>
#include <cstdio>

namespace Platform
{

static std::atomic<uint32_t> g_printCount{};

void Print(const char *, ...)
{
    g_printCount.fetch_add(1, std::memory_order_relaxed);
}

void *ModuleFactory(std::string_view)
{
    // Tests run without the game, so the gametypes interface must resolve to
    // nothing. This keeps the music kit gate on its conservative path.
    return nullptr;
}

bool UpdateGraffitiKey(std::string_view, const void *, const void *, size_t)
{
    return true;
}

}

S_API void S_CALLTYPE SteamAPI_RegisterCallback(CCallbackBase *, int)
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallback(CCallbackBase *)
{
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

class TestSteamNetworkingMessages final : public ISteamNetworkingMessages
{
public:
    EResult SendMessageToUser(const SteamNetworkingIdentity &identity, const void *data, uint32 size,
        int, int) override
    {
        sentSteamIds.push_back(identity.GetSteamID64());
        const auto *bytes = static_cast<const uint8_t *>(data);
        sentPayloads.emplace_back(bytes, bytes + size);
        return k_EResultOK;
    }

    int ReceiveMessagesOnChannel(int, SteamNetworkingMessage_t **, int) override
    {
        ++receiveCalls;
        return 0;
    }

    bool AcceptSessionWithUser(const SteamNetworkingIdentity &) override
    {
        return true;
    }

    bool CloseSessionWithUser(const SteamNetworkingIdentity &) override
    {
        return true;
    }

    bool CloseChannelWithUser(const SteamNetworkingIdentity &, int) override
    {
        return true;
    }

    ESteamNetworkingConnectionState GetSessionConnectionInfo(
        const SteamNetworkingIdentity &, SteamNetConnectionInfo_t *,
        SteamNetworkingQuickConnectionStatus *) override
    {
        return k_ESteamNetworkingConnectionState_None;
    }

    int receiveCalls{};
    std::vector<uint64_t> sentSteamIds;
    std::vector<std::vector<uint8_t>> sentPayloads;
};

static bool NetworkingClientRefreshesInterfacesAndSkipsIdlePolling()
{
    TestSteamNetworkingMessages first;
    TestSteamNetworkingMessages second;

    NetworkingClient networking{ &first };
    networking.Update(nullptr);

    const uint8_t ticket = 1;
    networking.SetAuthTicket(1, &ticket, sizeof(ticket));
    networking.Update(nullptr);
    networking.SetNetworkingMessages(&second);
    networking.Update(nullptr);
    networking.ClearAuthTicket(1);
    networking.Update(nullptr);

    return first.receiveCalls == 1 && second.receiveCalls == 1;
}

static bool NetworkingServerPreservesHostEventSteamId()
{
    TestSteamNetworkingMessages messages;
    NetworkingServer networking{ &messages };

    constexpr uint64_t SteamId = 76561198083722517ull;
    constexpr uint8_t Ticket = 1;
    networking.ClientConnected(SteamId, &Ticket, sizeof(Ticket));

    EventData event;
    event.type = static_cast<int>(HostEvent::NetMessage);
    event.id = SteamId;
    event.buffer = { 1, 2, 3, 4 };
    networking.SendHostEvent(event);

    return messages.sentSteamIds.size() == 2
        && messages.sentSteamIds.back() == SteamId
        && messages.sentPayloads.back() == event.buffer;
}

static bool WaitForHostMessage(ClientGC &gc, uint32_t type, EventData &result,
    uint64_t *microTransactionId = nullptr)
{
    std::vector<EventData> events;
    bool foundMessage = false;
    bool foundMicroTransaction = microTransactionId == nullptr;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (EventData &event : events)
        {
            if (!foundMessage
                && event.type == static_cast<int>(HostEvent::Message)
                && (event.id & ~ProtobufMask) == type)
            {
                result = std::move(event);
                foundMessage = true;
            }
            else if (microTransactionId
                && event.type == static_cast<int>(HostEvent::MicroTransactionResponse))
            {
                *microTransactionId = event.id;
                foundMicroTransaction = true;
            }
        }

        if (foundMessage && foundMicroTransaction)
        {
            return true;
        }

        events.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    return false;
}

static bool WaitForHostMessagesUntil(ClientGC &gc, uint32_t terminalType,
    std::vector<EventData> &result)
{
    std::vector<EventData> events;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        bool foundTerminal = false;
        for (EventData &event : events)
        {
            if (event.type != static_cast<int>(HostEvent::Message))
            {
                continue;
            }

            const uint32_t type = static_cast<uint32_t>(event.id) & ~ProtobufMask;
            foundTerminal |= type == terminalType;
            result.emplace_back(std::move(event));
        }

        if (foundTerminal)
        {
            return true;
        }

        events.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    return false;
}

static bool HostMessageNotReceived(ClientGC &gc, uint32_t type,
    std::chrono::milliseconds duration = std::chrono::milliseconds{ 100 })
{
    std::vector<EventData> events;
    auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (const EventData &event : events)
        {
            if (event.type == static_cast<int>(HostEvent::Message)
                && (event.id & ~ProtobufMask) == type)
            {
                return false;
            }
        }

        events.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    return true;
}

static bool HostMessageOrNetMessageNotReceived(ClientGC &gc, uint32_t type,
    std::chrono::milliseconds duration = std::chrono::seconds{ 1 })
{
    std::vector<EventData> events;
    auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (const EventData &event : events)
        {
            if ((event.type == static_cast<int>(HostEvent::Message)
                    || event.type == static_cast<int>(HostEvent::NetMessage))
                && (event.id & ~ProtobufMask) == type)
            {
                return false;
            }
        }

        events.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    return true;
}

template<typename T>
static bool ParseHostProtobuf(const EventData &event, T &message)
{
    GCMessageRead messageRead{ 0, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()) };
    return messageRead.IsValid() && messageRead.IsProtobuf() && messageRead.ReadProtobuf(message);
}

template<typename T>
static bool ParseHostJobProtobuf(const EventData &event, uint64_t jobId, T &message)
{
    constexpr size_t HeaderPrefixSize = sizeof(uint32_t) + sizeof(uint32_t);
    if (event.buffer.size() < HeaderPrefixSize)
    {
        return false;
    }

    const uint8_t *data = event.buffer.data();
    uint32_t type = 0;
    uint32_t headerSize = 0;
    std::memcpy(&type, data, sizeof(type));
    std::memcpy(&headerSize, data + sizeof(type), sizeof(headerSize));
    if (!(type & ProtobufMask)
        || !headerSize
        || HeaderPrefixSize + headerSize > event.buffer.size())
    {
        return false;
    }

    CMsgProtoBufHeader header;
    if (!header.ParseFromArray(data + HeaderPrefixSize, headerSize)
        || header.job_id_target() != jobId)
    {
        return false;
    }

    const size_t bodyOffset = HeaderPrefixSize + headerSize;
    return message.ParseFromArray(data + bodyOffset, event.buffer.size() - bodyOffset);
}

template<typename T>
static void SendGCProtobuf(ClientGC &gc, uint32_t type, const T &message)
{
    GCMessageWrite messageWrite{ type, message };
    gc.PostToGC(GCEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}

static bool SendGCProtobufJobData(ClientGC &gc, uint32_t type,
    const void *data, uint32_t size, uint64_t jobId)
{
    CMsgProtoBufHeader header;
    header.set_job_id_source(jobId);

    std::string headerData;
    if (!header.SerializeToString(&headerData))
    {
        return false;
    }

    uint32_t maskedType = type | ProtobufMask;
    GCMessageWrite messageWrite{ &maskedType, sizeof(maskedType) };
    messageWrite.WriteUint32(static_cast<uint32_t>(headerData.size()));
    messageWrite.WriteData(headerData.data(), static_cast<uint32_t>(headerData.size()));
    messageWrite.WriteData(data, size);
    gc.PostToGC(GCEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
    return true;
}

template<typename T>
static bool SendGCProtobufJob(ClientGC &gc, uint32_t type,
    const T &message, uint64_t jobId)
{
    std::string messageData;
    return message.SerializeToString(&messageData)
        && SendGCProtobufJobData(gc, type, messageData.data(),
            static_cast<uint32_t>(messageData.size()), jobId);
}

static bool IsMinimalPlayerProfile(
    const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &profile,
    uint32_t accountId)
{
    return profile.has_account_id()
        && profile.account_id() == accountId
        && !profile.has_ongoingmatch()
        && !profile.has_global_stats()
        && !profile.has_penalty_seconds()
        && !profile.has_penalty_reason()
        && !profile.has_vac_banned()
        && !profile.has_ranking()
        && !profile.has_commendation()
        && !profile.has_medals()
        && !profile.has_player_level()
        && !profile.has_player_cur_xp()
        && profile.rankings_size() == 0;
}

static bool PlayerProfileRequestsReturnMinimalProfiles()
{
    ClientGC gc{ 76561197960265729ull };

    CMsgGCCStrike15_v2_ClientRequestPlayersProfile request;
    request.set_account_id(123456);
    request.set_request_level(0x20);
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_ClientRequestPlayersProfile, request);

    EventData event;
    CMsgGCCStrike15_v2_PlayersProfile response;
    bool valid = WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_PlayersProfile, event)
        && ParseHostProtobuf(event, response)
        && !response.has_request_id()
        && response.account_profiles_size() == 1
        && IsMinimalPlayerProfile(response.account_profiles(0), 123456);

    request.Clear();
    request.set_request_id__deprecated(42);
    request.add_account_ids__deprecated(111);
    request.add_account_ids__deprecated(222);
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_ClientRequestPlayersProfile, request);

    event = {};
    response.Clear();
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_PlayersProfile, event)
        && ParseHostProtobuf(event, response)
        && response.has_request_id()
        && response.request_id() == 42
        && response.account_profiles_size() == 2
        && IsMinimalPlayerProfile(response.account_profiles(0), 111)
        && IsMinimalPlayerProfile(response.account_profiles(1), 222);

    return valid;
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

static bool StatsSubscriptionDuplicatesKeepNewest()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    constexpr uint32_t AccountId = SteamId & UINT32_MAX;
    constexpr uint64_t OldSubscriptionId = (uint64_t{ 3 } << 32) | AccountId;
    constexpr uint64_t NewSubscriptionId = (uint64_t{ 9 } << 32) | AccountId;
    constexpr uint64_t OtherItemId = (uint64_t{ 6 } << 32) | AccountId;
    constexpr const char *InventoryPath = "csgo_gc/inventory.txt";

    TestFilesystem::RemoveFile(InventoryPath);
    if (!TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    KeyValue inventoryKey{ "inventory" };
    inventoryKey.AddNumber("format_version", 1);
    KeyValue &items = inventoryKey.AddSubkey("items");
    auto addItem = [&](uint32_t highItemId, uint32_t defIndex, uint32_t position) {
        KeyValue &item = items.AddSubkey(std::to_string(highItemId));
        item.AddNumber("def_index", defIndex);
        item.AddNumber("inventory", position);
    };
    addItem(3, ItemSchema::ItemStatsSubscription, 30);
    addItem(6, 7, 60);
    addItem(9, ItemSchema::ItemStatsSubscription, 90);
    if (!inventoryKey.WriteToFile(InventoryPath))
    {
        TestFilesystem::RemoveFile(InventoryPath);
        TestFilesystem::RemoveDirectory("csgo_gc");
        return false;
    }

    bool valid = true;
    {
        Inventory inventory{ SteamId };
        const CSOEconItem *newSubscription = inventory.GetItem(NewSubscriptionId);
        std::optional<Inventory::StatsSubscriptionState> subscription
            = inventory.GetStatsSubscription();
        valid &= inventory.ItemCount() == 2
            && !inventory.GetItem(OldSubscriptionId)
            && newSubscription
            && newSubscription->def_index() == ItemSchema::ItemStatsSubscription
            && newSubscription->inventory() == 90
            && inventory.GetItem(OtherItemId)
            && subscription
            && subscription->itemId == NewSubscriptionId
            && subscription->timeInitiated != 0
            && subscription->timeNextCycle > subscription->timeInitiated;
    }

    KeyValue persisted{ "inventory" };
    const KeyValue *persistedItems = nullptr;
    valid &= persisted.ParseFromFile(InventoryPath);
    if (valid)
    {
        persistedItems = persisted.GetSubkey("items");
        const KeyValue *subscription = persisted.GetSubkey("stats_subscription");
        valid &= persistedItems
            && persistedItems->SubkeyCount() == 2
            && !persistedItems->GetSubkey("3")
            && persistedItems->GetSubkey("6")
            && persistedItems->GetSubkey("9")
            && persistedItems->GetSubkey("9")->GetNumber<uint32_t>("inventory") == 90
            && subscription
            && subscription->GetNumber<uint32_t>("time_initiated") != 0;
    }

    TestFilesystem::RemoveFile(InventoryPath);
    TestFilesystem::RemoveDirectory("csgo_gc");
    return valid;
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

static uint64_t DefaultEquipKey(uint32_t classId, uint32_t slotId)
{
    return (static_cast<uint64_t>(classId) << 32) | slotId;
}

static bool ApplyDefaultEquipUpdates(const CMsgSOMultipleObjects &update,
    std::unordered_map<uint64_t, uint32_t> &defaultEquips)
{
    for (const CMsgSOMultipleObjects_SingleObject &object : update.objects_modified())
    {
        if (object.type_id() != SOTypeDefaultEquippedDefinitionInstanceClient)
        {
            continue;
        }

        CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
        if (!defaultEquip.ParseFromString(object.object_data())
            || !defaultEquip.has_class_id() || !defaultEquip.has_slot_id())
        {
            return false;
        }

        uint64_t key = DefaultEquipKey(defaultEquip.class_id(), defaultEquip.slot_id());
        if (defaultEquip.item_definition())
        {
            defaultEquips[key] = defaultEquip.item_definition();
        }
        else
        {
            defaultEquips.erase(key);
        }
    }

    return true;
}

static bool DefaultEquipMatches(const std::unordered_map<uint64_t, uint32_t> &defaultEquips,
    uint32_t defIndex, uint32_t classId, uint32_t slotId)
{
    auto it = defaultEquips.find(DefaultEquipKey(classId, slotId));
    return it != defaultEquips.end() && it->second == defIndex;
}

static std::unordered_map<uint64_t, uint32_t> SnapshotDefaultEquips(Inventory &inventory)
{
    CMsgSOCacheSubscribed subscription;
    inventory.BuildCacheSubscription(subscription, false);

    std::unordered_map<uint64_t, uint32_t> defaultEquips;
    for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
    {
        if (type.type_id() != SOTypeDefaultEquippedDefinitionInstanceClient)
        {
            continue;
        }

        for (const std::string &data : type.object_data())
        {
            CSOEconDefaultEquippedDefinitionInstanceClient defaultEquip;
            if (defaultEquip.ParseFromString(data) && defaultEquip.item_definition())
            {
                defaultEquips[DefaultEquipKey(defaultEquip.class_id(), defaultEquip.slot_id())]
                    = defaultEquip.item_definition();
            }
        }
    }

    return defaultEquips;
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
    KeyValue &otherDefaultEquip = defaultEquips.AddSubkey("11");
    otherDefaultEquip.AddNumber("class_id", 2);
    otherDefaultEquip.AddNumber("slot_id", 6);
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
        std::unordered_map<uint64_t, uint32_t> clientDefaultEquips{
            { DefaultEquipKey(2, 4), 9 },
            { DefaultEquipKey(2, 6), 11 },
        };

        CMsgSOMultipleObjects unequip;
        valid &= inventory.EquipItem(Item1, 2, 0xffff, false, unequip);
        valid &= unequip.version() == version + 1 && inventory.Version() == unequip.version();
        version = inventory.Version();
        const CSOEconItem *item1 = inventory.GetItem(Item1);
        valid &= item1 && EquippedSlotForClass(*item1, 2) == -1
            && EquippedSlotForClass(*item1, 3) == 1;

        CMsgSOMultipleObjects unchangedUnequip;
        valid &= !inventory.EquipItem(Item1, 2, 0xffff, false, unchangedUnequip)
            && !unchangedUnequip.has_version()
            && unchangedUnequip.objects_modified_size() == 0;

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
            && ApplyDefaultEquipUpdates(defaultSwap, clientDefaultEquips)
            && DefaultEquipMatches(clientDefaultEquips, 9, 2, 3)
            && DefaultEquipMatches(clientDefaultEquips, 11, 2, 6)
            && !clientDefaultEquips.contains(DefaultEquipKey(2, 4));
        version = inventory.Version();
        item1 = inventory.GetItem(Item1);
        item2 = inventory.GetItem(Item2);
        valid &= item1 && item2
            && EquippedSlotForClass(*item1, 3) == 1
            && EquippedSlotForClass(*item2, 2) == 4;

        CMsgSOMultipleObjects defaultToDefaultSwap;
        valid &= inventory.EquipItem(DefaultItem, 2, 6, true, defaultToDefaultSwap);
        valid &= defaultToDefaultSwap.version() == version + 1
            && inventory.Version() == defaultToDefaultSwap.version()
            && ApplyDefaultEquipUpdates(defaultToDefaultSwap, clientDefaultEquips)
            && DefaultEquipMatches(clientDefaultEquips, 11, 2, 3)
            && DefaultEquipMatches(clientDefaultEquips, 9, 2, 6)
            && clientDefaultEquips == SnapshotDefaultEquips(inventory);
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
            bool foundPersona = false;
            for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
            {
                if (type.type_id() == SOTypeGameAccountClient && type.object_data_size() == 1)
                {
                    CSOEconGameAccountClient account;
                    foundAccount = account.ParseFromString(type.object_data(0));
                    valid &= foundAccount && !account.has_elevated_timestamp();
                    // no config.txt is present in the test environment, so the
                    // default frozen prime state must be reported
                    valid &= foundAccount && account.elevated_state() == ElevatedStatePrime;
                }
                else if (type.type_id() == SOTypePersonaDataPublic && type.object_data_size() == 1)
                {
                    CSOPersonaDataPublic personaData;
                    foundPersona = personaData.ParseFromString(type.object_data(0));
                    valid &= foundPersona && personaData.elevated_state();
                }
            }
            valid &= foundAccount && foundPersona;
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

static void RemoveCustomizationFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/gc_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteCustomizationFixtures()
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

    KeyValue &attributes = itemsGame.AddSubkey("attributes");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeStickerId0))
        .AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeStickerWear0))
        .AddString("attribute_type", "float");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeStickerScale0))
        .AddString("attribute_type", "float");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeStickerRotation0))
        .AddString("attribute_type", "float");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeCustomName))
        .AddString("attribute_type", "string");

    KeyValue &items = itemsGame.AddSubkey("items");
    KeyValue &weapon = items.AddSubkey("7");
    weapon.AddString("name", "weapon_ak47");
    weapon.AddString("item_rarity", "uncommon");
    KeyValue &weaponCapabilities = weapon.AddSubkey("capabilities");
    weaponCapabilities.AddNumber("can_sticker", 1);
    weaponCapabilities.AddNumber("nameable", 1);

    items.AddSubkey(std::to_string(ItemSchema::ItemSticker)).AddString("name", "sticker");
    items.AddSubkey("999").AddString("name", "name_tag");

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &inventoryItems = inventory.AddSubkey("items");

    auto addSticker = [&](const char *highItemId, uint32_t stickerKit)
    {
        KeyValue &item = inventoryItems.AddSubkey(highItemId);
        item.AddNumber("def_index", ItemSchema::ItemSticker);
        item.AddNumber("origin", ItemOriginTraded);
        item.AddNumber("rarity", ItemSchema::RarityCommon);
        item.AddSubkey("attributes")
            .AddNumber(std::to_string(ItemSchema::AttributeStickerId0), stickerKit);
    };
    auto addNameTag = [&](const char *highItemId)
    {
        KeyValue &item = inventoryItems.AddSubkey(highItemId);
        item.AddNumber("def_index", 999);
        item.AddNumber("origin", ItemOriginTraded);
        item.AddNumber("rarity", ItemSchema::RarityCommon);
    };

    addSticker("1", 101);
    addSticker("2", 102);
    addNameTag("3");
    addNameTag("4");

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt");
}

static bool ParseItemObject(const CMsgSOSingleObject &object, CSOEconItem &item)
{
    return object.type_id() == SOTypeItem && item.ParseFromString(object.object_data());
}

static bool ParseRecurringSubscriptionObject(const CMsgSOSingleObject &object,
    CSOAccountRecurringSubscription &subscription)
{
    return object.type_id() == SOTypeAccountRecurringSubscription
        && subscription.ParseFromString(object.object_data());
}

static bool HasAttribute(const CSOEconItem &item, uint32_t defIndex)
{
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        if (attribute.def_index() == defIndex)
        {
            return true;
        }
    }
    return false;
}

static bool GetUint32Attribute(const CSOEconItem &item, uint32_t defIndex, uint32_t &value)
{
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        if (attribute.def_index() == defIndex && attribute.value_bytes().size() == sizeof(value))
        {
            std::memcpy(&value, attribute.value_bytes().data(), sizeof(value));
            return true;
        }
    }

    return false;
}

static bool ScrapeStickerUntilRemoved(Inventory &inventory, uint64_t itemId,
    bool expectDestroyed, std::string_view expectedName)
{
    CMsgApplySticker scrape;
    scrape.set_item_item_id(itemId);
    scrape.set_sticker_slot(0);

    for (int i = 0; i < 12; i++)
    {
        CMsgSOSingleObject update;
        CMsgSOSingleObject destroy;
        CMsgGCItemCustomizationNotification notification;
        if (!inventory.ScrapeSticker(scrape, update, destroy, notification))
        {
            return false;
        }

        const CSOEconItem *item = inventory.GetItem(itemId);
        if (!item)
        {
            CSOEconItem destroyedItem;
            return expectDestroyed
                && !update.has_object_data()
                && ParseItemObject(destroy, destroyedItem)
                && destroyedItem.id() == itemId
                && notification.request() == k_EGCItemCustomizationNotification_RemoveSticker
                && notification.item_id_size() == 1
                && notification.item_id(0) == (ItemIdDefaultItemMask | 7);
        }

        if (!HasAttribute(*item, ItemSchema::AttributeStickerId0))
        {
            CSOEconItem updatedItem;
            return !expectDestroyed
                && ParseItemObject(update, updatedItem)
                && !destroy.has_object_data()
                && updatedItem.id() == itemId
                && inventory.GetCustomName(updatedItem) == expectedName
                && HasAttribute(updatedItem, ItemSchema::AttributeCustomName)
                && updatedItem.attribute_size() == 1
                && notification.request() == k_EGCItemCustomizationNotification_RemoveSticker
                && notification.item_id_size() == 1
                && notification.item_id(0) == itemId;
        }
    }

    return false;
}

static bool BaseItemCustomizationsPreserveRemainingState()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveCustomizationFixtures();
    if (!WriteCustomizationFixtures())
    {
        RemoveCustomizationFixtures();
        return false;
    }

    bool valid = true;
    {
        Inventory inventory{ SteamId };

        CMsgApplySticker applyToBase;
        applyToBase.set_sticker_item_id(itemId(1));
        applyToBase.set_sticker_slot(0);
        applyToBase.set_baseitem_defidx(7);
        CMsgSOSingleObject stickerCreate;
        CMsgSOSingleObject stickerDestroy;
        CMsgGCItemCustomizationNotification stickerNotification;
        valid &= inventory.ApplySticker(applyToBase, stickerCreate, stickerDestroy,
            stickerNotification);

        CSOEconItem stickerClone;
        valid &= ParseItemObject(stickerCreate, stickerClone)
            && stickerClone.origin() == ItemOriginBaseItem
            && stickerClone.rarity() == ItemSchema::RarityDefault;

        CMsgSOSingleObject nameUpdate;
        CMsgSOSingleObject nameTagDestroy;
        CMsgGCItemCustomizationNotification nameNotification;
        valid &= inventory.NameItem(itemId(3), stickerClone.id(), "named",
            nameUpdate, nameTagDestroy, nameNotification);
        valid &= ScrapeStickerUntilRemoved(inventory, stickerClone.id(), false, "named");

        CMsgSOSingleObject removeNameUpdate;
        CMsgSOSingleObject removeNameDestroy;
        CMsgGCItemCustomizationNotification removeNameNotification;
        valid &= inventory.RemoveItemName(stickerClone.id(), removeNameUpdate,
            removeNameDestroy, removeNameNotification);
        CSOEconItem destroyedStickerClone;
        valid &= !inventory.GetItem(stickerClone.id())
            && !removeNameUpdate.has_object_data()
            && ParseItemObject(removeNameDestroy, destroyedStickerClone)
            && destroyedStickerClone.id() == stickerClone.id();

        CMsgSOSingleObject nameCreate;
        CMsgSOSingleObject secondNameTagDestroy;
        CMsgGCItemCustomizationNotification secondNameNotification;
        valid &= inventory.NameBaseItem(itemId(4), 7, "named", nameCreate,
            secondNameTagDestroy, secondNameNotification);

        CSOEconItem nameClone;
        valid &= ParseItemObject(nameCreate, nameClone)
            && nameClone.origin() == ItemOriginBaseItem
            && nameClone.rarity() == ItemSchema::RarityDefault;

        CMsgApplySticker applyToNamedClone;
        applyToNamedClone.set_sticker_item_id(itemId(2));
        applyToNamedClone.set_sticker_slot(0);
        applyToNamedClone.set_item_item_id(nameClone.id());
        CMsgSOSingleObject secondStickerUpdate;
        CMsgSOSingleObject secondStickerDestroy;
        CMsgGCItemCustomizationNotification secondStickerNotification;
        valid &= inventory.ApplySticker(applyToNamedClone, secondStickerUpdate,
            secondStickerDestroy, secondStickerNotification);

        CMsgSOSingleObject secondRemoveNameUpdate;
        CMsgSOSingleObject secondRemoveNameDestroy;
        CMsgGCItemCustomizationNotification secondRemoveNameNotification;
        valid &= inventory.RemoveItemName(nameClone.id(), secondRemoveNameUpdate,
            secondRemoveNameDestroy, secondRemoveNameNotification);
        const CSOEconItem *unnamedClone = inventory.GetItem(nameClone.id());
        CSOEconItem updatedUnnamedClone;
        valid &= unnamedClone
            && inventory.GetCustomName(*unnamedClone).empty()
            && HasAttribute(*unnamedClone, ItemSchema::AttributeStickerId0)
            && ParseItemObject(secondRemoveNameUpdate, updatedUnnamedClone)
            && !secondRemoveNameDestroy.has_object_data();

        valid &= ScrapeStickerUntilRemoved(inventory, nameClone.id(), true, {});
    }

    RemoveCustomizationFixtures();
    return valid;
}

static void RemoveStatTrakFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/gc_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteStatTrakFixtures()
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
    KeyValue &attributes = itemsGame.AddSubkey("attributes");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeKillEater))
        .AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeKillEaterScoreType))
        .AddNumber("stored_as_integer", 1);

    KeyValue &items = itemsGame.AddSubkey("items");
    items.AddSubkey(std::to_string(ItemSchema::ItemStatTrakSwapTool))
        .AddString("name", "stattrak_swap_tool");
    KeyValue &bundle = items.AddSubkey(std::to_string(ItemSchema::ItemStatTrakSwapToolBundle));
    bundle.AddString("name", "crate_stattrak_swap_tool");
    bundle.AddString("loot_list_name", "bundle_tool_stattrak_swap");

    auto addKnifeDefinition = [&](uint32_t defIndex, const char *name)
    {
        KeyValue &knife = items.AddSubkey(std::to_string(defIndex));
        knife.AddString("name", name);
        knife.AddSubkey("capabilities").AddNumber("can_stattrack_swap", 1);
    };
    addKnifeDefinition(500, "weapon_knife");
    addKnifeDefinition(507, "weapon_knife_karambit");

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &inventoryItems = inventory.AddSubkey("items");

    auto addItem = [&](const char *highItemId, uint32_t defIndex, uint32_t quality,
        uint32_t rarity)
    {
        KeyValue &item = inventoryItems.AddSubkey(highItemId);
        item.AddNumber("def_index", defIndex);
        item.AddNumber("quality", quality);
        item.AddNumber("origin", ItemOriginTraded);
        item.AddNumber("rarity", rarity);
        return &item;
    };

    addItem("1", ItemSchema::ItemStatTrakSwapToolBundle,
        ItemSchema::QualityUnique, ItemSchema::RarityCommon);
    addItem("2", ItemSchema::ItemStatTrakSwapTool,
        ItemSchema::QualityUnique, ItemSchema::RarityCommon);

    KeyValue *karambit = addItem("3", 507,
        ItemSchema::QualityUnusual, ItemSchema::RarityAncient);
    KeyValue &karambitAttributes = karambit->AddSubkey("attributes");
    karambitAttributes.AddNumber(std::to_string(ItemSchema::AttributeKillEater), 7);
    karambitAttributes.AddNumber(std::to_string(ItemSchema::AttributeKillEaterScoreType), 0);

    KeyValue *bayonet = addItem("4", 500,
        ItemSchema::QualityUnusual, ItemSchema::RarityAncient);
    KeyValue &bayonetAttributes = bayonet->AddSubkey("attributes");
    bayonetAttributes.AddNumber(std::to_string(ItemSchema::AttributeKillEater), 60);
    bayonetAttributes.AddNumber(std::to_string(ItemSchema::AttributeKillEaterScoreType), 0);

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt");
}

static bool StatTrakSwapToolTwoPackCreatesTwoTools()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveStatTrakFixtures();
    if (!WriteStatTrakFixtures())
    {
        RemoveStatTrakFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        GCMessageWrite request{ k_EMsgGCUnlockCrate };
        request.WriteUint64(0);
        request.WriteUint64(itemId(1));
        gc.PostToGC(GCEvent::Message, request.TypeMasked(), request.Data(), request.Size());

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc, k_EMsgGCItemCustomizationNotification, events);

        int createCount = 0;
        int destroyCount = 0;
        int notificationCount = 0;
        std::unordered_set<uint64_t> createdIds;
        std::unordered_set<uint64_t> notifiedIds;
        for (const EventData &event : events)
        {
            const uint32_t type = static_cast<uint32_t>(event.id) & ~ProtobufMask;
            if (type == k_ESOMsg_Create)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                const bool parsed = ParseHostProtobuf(event, object) && ParseItemObject(object, item);
                valid &= parsed;
                if (parsed)
                {
                    valid &= item.def_index() == ItemSchema::ItemStatTrakSwapTool
                        && item.origin() == ItemOriginCrate
                        && createdIds.insert(item.id()).second;
                }
                ++createCount;
            }
            else if (type == k_ESOMsg_Destroy)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                valid &= ParseHostProtobuf(event, object)
                    && ParseItemObject(object, item)
                    && item.id() == itemId(1);
                ++destroyCount;
            }
            else if (type == k_EMsgGCItemCustomizationNotification)
            {
                CMsgGCItemCustomizationNotification notification;
                const bool parsed = ParseHostProtobuf(event, notification);
                valid &= parsed;
                if (parsed)
                {
                    valid &= notification.request()
                        == k_EGCItemCustomizationNotification_UnlockCrate;
                    for (uint64_t id : notification.item_id())
                    {
                        notifiedIds.insert(id);
                    }
                }
                ++notificationCount;
            }
        }

        valid &= createCount == 2
            && destroyCount == 1
            && notificationCount == 1
            && createdIds.size() == 2
            && createdIds == notifiedIds;
    }

    RemoveStatTrakFixtures();
    return valid;
}

static bool UnusualStatTrakKnivesCanSwapCounters()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveStatTrakFixtures();
    if (!WriteStatTrakFixtures())
    {
        RemoveStatTrakFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgApplyStatTrakSwap request;
        request.set_tool_item_id(itemId(2));
        request.set_item_1_item_id(itemId(3));
        request.set_item_2_item_id(itemId(4));
        SendGCProtobuf(gc, k_EMsgGCStatTrakSwap, request);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc, k_EMsgGCItemCustomizationNotification, events);

        int updateCount = 0;
        int destroyCount = 0;
        int notificationCount = 0;
        const std::unordered_set<uint64_t> expectedWeaponIds{ itemId(3), itemId(4) };
        std::unordered_set<uint64_t> updatedIds;
        std::unordered_set<uint64_t> notifiedIds;
        for (const EventData &event : events)
        {
            const uint32_t type = static_cast<uint32_t>(event.id) & ~ProtobufMask;
            if (type == k_ESOMsg_Update)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                const bool parsed = ParseHostProtobuf(event, object) && ParseItemObject(object, item);
                valid &= parsed;
                if (parsed)
                {
                    uint32_t counter = 0;
                    valid &= updatedIds.insert(item.id()).second
                        && item.quality() == ItemSchema::QualityUnusual
                        && GetUint32Attribute(item, ItemSchema::AttributeKillEater, counter);
                    if (item.id() == itemId(3))
                    {
                        valid &= counter == 60;
                    }
                    else if (item.id() == itemId(4))
                    {
                        valid &= counter == 7;
                    }
                    else
                    {
                        valid = false;
                    }
                }
                ++updateCount;
            }
            else if (type == k_ESOMsg_Destroy)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                valid &= ParseHostProtobuf(event, object)
                    && ParseItemObject(object, item)
                    && item.id() == itemId(2);
                ++destroyCount;
            }
            else if (type == k_EMsgGCItemCustomizationNotification)
            {
                CMsgGCItemCustomizationNotification notification;
                const bool parsed = ParseHostProtobuf(event, notification);
                valid &= parsed;
                if (parsed)
                {
                    valid &= notification.request()
                        == k_EGCItemCustomizationNotification_StatTrakSwap;
                    for (uint64_t id : notification.item_id())
                    {
                        notifiedIds.insert(id);
                    }
                }
                ++notificationCount;
            }
        }

        valid &= updateCount == 2
            && destroyCount == 1
            && notificationCount == 1
            && updatedIds == expectedWeaponIds
            && notifiedIds == expectedWeaponIds;
    }

    RemoveStatTrakFixtures();
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
    KeyValue &crate = items.AddSubkey("8");
    crate.AddString("name", "crate_test");
    crate.AddString("loot_list_name", "crate_test");
    KeyValue &coupon = items.AddSubkey("9");
    coupon.AddString("name", "coupon_test");
    coupon.AddString("item_type", "coupon");
    coupon.AddString("loot_list_name", "coupon_test_loot");
    KeyValue &unavailableCoupon = items.AddSubkey("10");
    unavailableCoupon.AddString("name", "coupon_unavailable");
    unavailableCoupon.AddString("item_type", "coupon");
    unavailableCoupon.AddString("loot_list_name", "coupon_missing_loot");
    KeyValue &fallbackCoupon = items.AddSubkey("11");
    fallbackCoupon.AddString("name", "coupon_fallback");
    fallbackCoupon.AddString("item_type", "coupon");
    fallbackCoupon.AddString("loot_list_name", "coupon_test_loot");
    items.AddSubkey(std::to_string(ItemSchema::ItemStatsSubscription))
        .AddString("name", "subscription1");
    KeyValue &lootLists = itemsGame.AddSubkey("client_loot_lists");
    lootLists.AddSubkey("coupon_test_loot").AddNumber("weapon_ak47", 1);

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    unusualLootLists.AddSubkey("empty");

    KeyValue priceSheet{ "price_sheet" };
    KeyValue &store = priceSheet.AddSubkey("store");
    store.AddNumber("featured_item_index", 7);
    KeyValue &bannerLayout = store.AddSubkey("store_banner_layout");
    bannerLayout.AddSubkey("8").AddNumber("market_link", 1);
    KeyValue &couponBanner = bannerLayout.AddSubkey("9");
    couponBanner.AddString("custom_format", "coupon");
    couponBanner.AddNumber("linked_coupon", 10);
    bannerLayout.AddSubkey("10").AddString("custom_format", "coupon");
    bannerLayout.AddSubkey("11").AddString("custom_format", "coupon");
    bannerLayout.AddSubkey("9999").AddString("custom_format", "double");
    KeyValue &entries = store.AddSubkey("entries");
    KeyValue &priceTemplate = entries.AddSubkey("offline_price_template");
    priceTemplate.AddString("item_link", "weapon_ak47");
    KeyValue &prices = priceTemplate.AddSubkey("prices");
    prices.AddNumber("USD", 99);
    prices.AddNumber("CNY", 700);
    KeyValue &couponEntry = entries.AddSubkey("coupon_test");
    couponEntry.AddString("item_link", "coupon_test");
    KeyValue &couponPrices = couponEntry.AddSubkey("prices");
    couponPrices.AddNumber("USD", 123);
    couponPrices.AddNumber("CNY", 888);

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
        constexpr std::string_view DisabledMarketLink{
            "market_link\0" "0\0", sizeof("market_link\0" "0\0") - 1
        };
        constexpr std::string_view CrateEntryWithPrices{
            "\0" "crate_test\0"
            "\1" "item_link\0" "crate_test\0"
            "\1" "category_tags\0" "Misc\0"
            "\0" "prices\0"
            "\1" "USD\0" "99\0"
            "\1" "CNY\0" "700\0"
            "\x0b" "\x0b",
            sizeof("\0" "crate_test\0"
                "\1" "item_link\0" "crate_test\0"
                "\1" "category_tags\0" "Misc\0"
                "\0" "prices\0"
                "\1" "USD\0" "99\0"
                "\1" "CNY\0" "700\0"
                "\x0b" "\x0b") - 1
        };
        valid &= WaitForHostMessage(gc, k_EMsgGCStoreGetUserDataResponse, event)
            && ParseHostProtobuf(event, storeDataResponse)
            && storeDataResponse.result() == 1
            && storeDataResponse.price_sheet_version() == priceSheetVersion
            && !storeDataResponse.price_sheet().empty()
            && storeDataResponse.price_sheet().find("crate_test") != std::string::npos
            && storeDataResponse.price_sheet().find(DisabledMarketLink) != std::string::npos
            && storeDataResponse.price_sheet().find(CrateEntryWithPrices) != std::string::npos;

        CMsgGCStorePurchaseInit purchaseInit;
        CGCStorePurchaseInit_LineItem *lineItem = purchaseInit.add_line_items();
        lineItem->set_item_def_id(8);
        lineItem->set_quantity(2);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchaseInit);
        event = {};
        uint64_t authorizationTransactionId = 0;
        CMsgGCStorePurchaseInitResponse initResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event,
                &authorizationTransactionId)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() == 1
            && initResponse.txn_id() != 0
            && authorizationTransactionId == initResponse.txn_id()
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

            std::unordered_map<uint64_t, uint32_t> snapshotItems;
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
                        snapshotItems.emplace(item.id(), item.def_index());
                    }
                }
            }

            valid &= snapshotItems.size() == 2;
            for (uint64_t itemId : finalizeResponse.item_ids())
            {
                auto item = snapshotItems.find(itemId);
                valid &= item != snapshotItems.end() && item->second == 8;
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

static bool LegacyStorePriceSheetMatchesClientSchema()
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

        CMsgStoreGetUserData storeData;
        storeData.set_price_sheet_version(0);
        SendGCProtobuf(gc, k_EMsgGCStoreGetUserData, storeData);

        EventData event;
        CMsgStoreGetUserDataResponse response;
        constexpr std::string_view UnavailableCouponBanner{
            "\0" "10\0" "\1" "custom_format\0" "coupon\0",
            sizeof("\0" "10\0" "\1" "custom_format\0" "coupon\0") - 1
        };
        constexpr std::string_view UnknownBanner{
            "\0" "9999\0" "\1" "custom_format\0" "double\0",
            sizeof("\0" "9999\0" "\1" "custom_format\0" "double\0") - 1
        };
        constexpr std::string_view LinkedCoupon{
            "linked_coupon\0" "10\0",
            sizeof("linked_coupon\0" "10\0") - 1
        };
        constexpr std::string_view ExistingCouponPrice{
            "\0" "coupon_test\0"
            "\1" "item_link\0" "coupon_test\0"
            "\0" "prices\0"
            "\1" "USD\0" "123\0"
            "\1" "CNY\0" "888\0",
            sizeof("\0" "coupon_test\0"
                "\1" "item_link\0" "coupon_test\0"
                "\0" "prices\0"
                "\1" "USD\0" "123\0"
                "\1" "CNY\0" "888\0") - 1
        };
        constexpr std::string_view FallbackCouponPrice{
            "\0" "coupon_fallback\0"
            "\1" "item_link\0" "coupon_fallback\0"
            "\1" "category_tags\0" "Misc\0"
            "\0" "prices\0"
            "\1" "USD\0" "99\0"
            "\1" "CNY\0" "700\0",
            sizeof("\0" "coupon_fallback\0"
                "\1" "item_link\0" "coupon_fallback\0"
                "\1" "category_tags\0" "Misc\0"
                "\0" "prices\0"
                "\1" "USD\0" "99\0"
                "\1" "CNY\0" "700\0") - 1
        };
        valid &= WaitForHostMessage(gc, k_EMsgGCStoreGetUserDataResponse, event)
            && ParseHostProtobuf(event, response)
            && response.result() == 1
            && response.price_sheet().find(UnavailableCouponBanner) == std::string::npos
            && response.price_sheet().find(UnknownBanner) == std::string::npos
            && response.price_sheet().find(LinkedCoupon) == std::string::npos
            && response.price_sheet().find(ExistingCouponPrice) != std::string::npos
            && response.price_sheet().find(FallbackCouponPrice) != std::string::npos;

        CMsgGCStorePurchaseInit invalidPurchase;
        CGCStorePurchaseInit_LineItem *lineItem = invalidPurchase.add_line_items();
        lineItem->set_item_def_id(10);
        lineItem->set_quantity(1);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, invalidPurchase);
        event = {};
        CMsgGCStorePurchaseInitResponse initResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() != 1;

        CMsgGCStorePurchaseInit purchase;
        lineItem = purchase.add_line_items();
        lineItem->set_item_def_id(9);
        lineItem->set_quantity(1);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchase);
        event = {};
        initResponse.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() == 1
            && initResponse.txn_id() != 0;

        CMsgGCStorePurchaseFinalize finalize;
        finalize.set_txn_id(initResponse.txn_id());
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseFinalize, finalize);

        std::vector<EventData> finalizeEvents;
        CMsgGCStorePurchaseFinalizeResponse finalizeResponse;
        CMsgSOSingleObject create;
        CSOEconItem purchasedItem;
        bool foundFinalize = false;
        bool foundCreate = false;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCStorePurchaseFinalizeResponse, finalizeEvents);
        for (const EventData &finalizeEvent : finalizeEvents)
        {
            uint32_t type = static_cast<uint32_t>(finalizeEvent.id) & ~ProtobufMask;
            if (type == k_ESOMsg_Create)
            {
                foundCreate = ParseHostProtobuf(finalizeEvent, create)
                    && ParseItemObject(create, purchasedItem);
            }
            else if (type == k_EMsgGCStorePurchaseFinalizeResponse)
            {
                foundFinalize = ParseHostProtobuf(finalizeEvent, finalizeResponse);
            }
        }

        valid &= foundFinalize
            && foundCreate
            && finalizeResponse.result() == 1
            && finalizeResponse.item_ids_size() == 1
            && purchasedItem.id() == finalizeResponse.item_ids(0)
            && purchasedItem.def_index() == 7;
    }

    RemoveStoreFixtures();
    return valid;
}

static bool StatsSubscriptionPurchasesRejectDuplicates()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveStoreFixtures();
    if (!WriteStoreFixtures())
    {
        RemoveStoreFixtures();
        return false;
    }

    bool valid = true;
    uint64_t subscriptionItemId = 0;
    {
        ClientGC gc{ SteamId };
        EventData event;
        CMsgGCStorePurchaseInitResponse initResponse;

        CMsgGCStorePurchaseInit invalidQuantity;
        CGCStorePurchaseInit_LineItem *lineItem = invalidQuantity.add_line_items();
        lineItem->set_item_def_id(ItemSchema::ItemStatsSubscription);
        lineItem->set_quantity(2);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, invalidQuantity);
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() != 1
            && initResponse.item_ids_size() == 0;

        CMsgGCStorePurchaseInit purchaseInit;
        lineItem = purchaseInit.add_line_items();
        lineItem->set_item_def_id(ItemSchema::ItemStatsSubscription);
        lineItem->set_quantity(1);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchaseInit);
        event = {};
        initResponse.Clear();
        uint64_t authorizationTransactionId = 0;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event,
                &authorizationTransactionId)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() == 1
            && initResponse.txn_id() != 0
            && authorizationTransactionId == initResponse.txn_id();

        CMsgGCStorePurchaseFinalize finalize;
        finalize.set_txn_id(initResponse.txn_id());
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseFinalize, finalize);

        std::vector<EventData> finalizeEvents;
        CMsgGCStorePurchaseFinalizeResponse finalizeResponse;
        CSOEconItem purchasedItem;
        CSOAccountRecurringSubscription recurringSubscription;
        CMsgGCHRecurringSubscriptionStatusChange status;
        bool foundItemCreate = false;
        bool foundSubscriptionCreate = false;
        bool foundFinalize = false;
        bool foundStatus = false;
        size_t itemCreateIndex = SIZE_MAX;
        size_t subscriptionCreateIndex = SIZE_MAX;
        size_t finalizeIndex = SIZE_MAX;
        size_t statusIndex = SIZE_MAX;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCRecurringSubscriptionStatus, finalizeEvents);
        for (size_t i = 0; i < finalizeEvents.size(); i++)
        {
            const EventData &finalizeEvent = finalizeEvents[i];
            const uint32_t type = static_cast<uint32_t>(finalizeEvent.id) & ~ProtobufMask;
            if (type == k_ESOMsg_Create)
            {
                CMsgSOSingleObject create;
                if (!ParseHostProtobuf(finalizeEvent, create))
                {
                    valid = false;
                }
                else if (ParseItemObject(create, purchasedItem))
                {
                    foundItemCreate = true;
                    itemCreateIndex = i;
                }
                else if (ParseRecurringSubscriptionObject(create, recurringSubscription))
                {
                    foundSubscriptionCreate = true;
                    subscriptionCreateIndex = i;
                }
            }
            else if (type == k_EMsgGCStorePurchaseFinalizeResponse)
            {
                foundFinalize = ParseHostProtobuf(finalizeEvent, finalizeResponse);
                finalizeIndex = i;
            }
            else if (type == k_EMsgGCRecurringSubscriptionStatus)
            {
                foundStatus = ParseHostProtobuf(finalizeEvent, status);
                statusIndex = i;
            }
        }

        valid &= foundItemCreate
            && foundSubscriptionCreate
            && foundFinalize
            && foundStatus
            && itemCreateIndex < subscriptionCreateIndex
            && subscriptionCreateIndex < finalizeIndex
            && finalizeIndex < statusIndex
            && finalizeResponse.result() == 1
            && finalizeResponse.item_ids_size() == 1;
        if (finalizeResponse.item_ids_size() == 1)
        {
            subscriptionItemId = finalizeResponse.item_ids(0);
        }
        valid &= subscriptionItemId
            && purchasedItem.id() == subscriptionItemId
            && purchasedItem.def_index() == ItemSchema::ItemStatsSubscription
            && recurringSubscription.time_initiated() != 0
            && recurringSubscription.time_next_cycle() > recurringSubscription.time_initiated()
            && status.steamid() == SteamId
            && status.agreementid() == subscriptionItemId
            && status.active();

        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchaseInit);
        event = {};
        initResponse.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() != 1
            && initResponse.item_ids_size() == 0;
    }

    {
        ClientGC gc{ SteamId };

        CMsgClientHello hello;
        SendGCProtobuf(gc, k_EMsgGCClientHello, hello);

        std::vector<EventData> helloEvents;
        CMsgClientWelcome welcome;
        CMsgGCHRecurringSubscriptionStatusChange status;
        CMsgGCCStrike15_ClientDeepStats deepStats;
        bool foundWelcome = false;
        bool foundCachedSubscription = false;
        uint32_t cachedTimeInitiated = 0;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCCStrike15_v2_ClientGCRankUpdate, helloEvents);
        for (const EventData &helloEvent : helloEvents)
        {
            const uint32_t type = static_cast<uint32_t>(helloEvent.id) & ~ProtobufMask;
            if (type == k_EMsgGCClientWelcome)
            {
                foundWelcome = ParseHostProtobuf(helloEvent, welcome);
                if (foundWelcome && welcome.outofdate_subscribed_caches_size() == 1)
                {
                    for (const CMsgSOCacheSubscribed_SubscribedType &objects
                        : welcome.outofdate_subscribed_caches(0).objects())
                    {
                        if (objects.type_id() != SOTypeAccountRecurringSubscription
                            || objects.object_data_size() != 1)
                        {
                            continue;
                        }

                        CSOAccountRecurringSubscription cachedSubscription;
                        foundCachedSubscription
                            = cachedSubscription.ParseFromString(objects.object_data(0));
                        if (foundCachedSubscription)
                        {
                            cachedTimeInitiated = cachedSubscription.time_initiated();
                            valid &= cachedTimeInitiated != 0
                                && cachedSubscription.time_next_cycle() > cachedTimeInitiated;
                        }
                    }
                }
            }
        }

        valid &= foundWelcome
            && foundCachedSubscription
            && HostMessageNotReceived(gc, k_EMsgGCRecurringSubscriptionStatus)
            && HostMessageNotReceived(gc, k_EMsgGCCStrike15_ClientDeepStats);

        constexpr uint64_t SubscriptionStatusJobId = 4748001;
        CMsgGCHRecurringSubscriptionStatusChange statusRequest;
        valid &= SendGCProtobufJob(gc, k_EMsgGCRecurringSubscriptionStatus,
            statusRequest, SubscriptionStatusJobId);
        EventData event;
        status.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCRecurringSubscriptionStatus, event)
            && ParseHostJobProtobuf(event, SubscriptionStatusJobId, status)
            && status.steamid() == SteamId
            && status.agreementid() == subscriptionItemId
            && status.active();

        constexpr uint64_t DeepStatsJobId = 4748002;
        CMsgGCCStrike15_ClientDeepStats deepStatsRequest;
        deepStatsRequest.set_account_id(SteamId & UINT32_MAX);
        deepStatsRequest.mutable_range()->set_begin(cachedTimeInitiated);
        deepStatsRequest.mutable_range()->set_end(cachedTimeInitiated + 100);
        deepStatsRequest.mutable_range()->set_frozen(false);
        valid &= SendGCProtobufJob(gc, k_EMsgGCCStrike15_ClientDeepStats,
            deepStatsRequest, DeepStatsJobId);
        event = {};
        deepStats.Clear();
        valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_ClientDeepStats, event)
            && ParseHostJobProtobuf(event, DeepStatsJobId, deepStats)
            && deepStats.account_id() == (SteamId & UINT32_MAX)
            && deepStats.has_range()
            && deepStats.range().begin() == cachedTimeInitiated
            && deepStats.range().end() == cachedTimeInitiated + 100
            && deepStats.range().frozen()
            && deepStats.matches_size() == 0;

        CMsgGCStorePurchaseInit purchaseInit;
        CGCStorePurchaseInit_LineItem *lineItem = purchaseInit.add_line_items();
        lineItem->set_item_def_id(ItemSchema::ItemStatsSubscription);
        lineItem->set_quantity(1);
        SendGCProtobuf(gc, k_EMsgGCStorePurchaseInit, purchaseInit);

        event = {};
        CMsgGCStorePurchaseInitResponse initResponse;
        valid &= WaitForHostMessage(gc, k_EMsgGCStorePurchaseInitResponse, event)
            && ParseHostProtobuf(event, initResponse)
            && initResponse.result() != 1;
    }

    KeyValue persisted{ "inventory" };
    valid &= subscriptionItemId
        && persisted.ParseFromFile("csgo_gc/inventory.txt");
    if (valid)
    {
        const KeyValue *items = persisted.GetSubkey("items");
        size_t subscriptionCount = 0;
        if (items)
        {
            for (const KeyValue &item : *items)
            {
                subscriptionCount += item.GetNumber<uint32_t>("def_index")
                    == ItemSchema::ItemStatsSubscription;
            }
        }
        valid &= items
            && subscriptionCount == 1
            && items->GetSubkey(std::to_string(subscriptionItemId >> 32));
    }

    {
        ClientGC gc{ SteamId };
        GCMessageWrite deleteRequest{ k_EMsgGCDelete };
        deleteRequest.WriteUint64(subscriptionItemId);
        gc.PostToGC(GCEvent::Message, deleteRequest.TypeMasked(),
            deleteRequest.Data(), deleteRequest.Size());

        std::vector<EventData> deleteEvents;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCRecurringSubscriptionStatus, deleteEvents);

        bool foundItemDestroy = false;
        bool foundSubscriptionDestroy = false;
        bool foundInactiveStatus = false;
        size_t itemDestroyIndex = SIZE_MAX;
        size_t subscriptionDestroyIndex = SIZE_MAX;
        size_t statusIndex = SIZE_MAX;
        for (size_t i = 0; i < deleteEvents.size(); ++i)
        {
            const EventData &deleteEvent = deleteEvents[i];
            const uint32_t type = static_cast<uint32_t>(deleteEvent.id) & ~ProtobufMask;
            if (type == k_ESOMsg_Destroy)
            {
                CMsgSOSingleObject object;
                if (!ParseHostProtobuf(deleteEvent, object))
                {
                    valid = false;
                    continue;
                }

                CSOEconItem item;
                CSOAccountRecurringSubscription subscription;
                if (ParseItemObject(object, item))
                {
                    foundItemDestroy = item.id() == subscriptionItemId;
                    itemDestroyIndex = i;
                }
                else if (ParseRecurringSubscriptionObject(object, subscription))
                {
                    foundSubscriptionDestroy = true;
                    subscriptionDestroyIndex = i;
                }
            }
            else if (type == k_EMsgGCRecurringSubscriptionStatus)
            {
                CMsgGCHRecurringSubscriptionStatusChange status;
                foundInactiveStatus = ParseHostProtobuf(deleteEvent, status)
                    && status.steamid() == SteamId
                    && status.agreementid() == 0
                    && !status.active();
                statusIndex = i;
            }
        }

        valid &= foundItemDestroy
            && foundSubscriptionDestroy
            && foundInactiveStatus
            && itemDestroyIndex < subscriptionDestroyIndex
            && subscriptionDestroyIndex < statusIndex;
    }

    KeyValue afterDelete{ "inventory" };
    valid &= afterDelete.ParseFromFile("csgo_gc/inventory.txt")
        && !afterDelete.GetSubkey("stats_subscription");
    if (valid)
    {
        const KeyValue *items = afterDelete.GetSubkey("items");
        if (items)
        {
            for (const KeyValue &item : *items)
            {
                valid &= item.GetNumber<uint32_t>("def_index")
                    != ItemSchema::ItemStatsSubscription;
            }
        }
    }

    RemoveStoreFixtures();
    return valid;
}

static void RemovePrestigeFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveFile("csgo/steam.inf");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteTextFile(const char *path, std::string_view contents)
{
    FILE *file = fopen(path, "wb");
    if (!file)
    {
        return false;
    }

    bool written = fwrite(contents.data(), 1, contents.size(), file) == contents.size();
    written &= fclose(file) == 0;
    return written;
}

static bool WritePrestigeFixtures(std::string_view versionDate)
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
    itemsGame.AddSubkey("prefabs")
        .AddSubkey("prestige_coin")
        .AddString("item_type", "prestige_coin");

    KeyValue &items = itemsGame.AddSubkey("items");
    auto addMedal = [&](uint32_t defIndex, uint32_t year)
    {
        KeyValue &item = items.AddSubkey(std::to_string(defIndex));
        item.AddString("name", std::string{ "prestige coin " }.append(std::to_string(year)));
        item.AddString("prefab", "prestige_coin");
        item.AddSubkey("attributes").AddNumber("prestige year", year);
    };

    addMedal(1376, 2019);
    addMedal(1377, 2019);
    addMedal(4873, 2023);
    addMedal(4874, 2023);

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    unusualLootLists.AddSubkey("empty");

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &playerState = inventory.AddSubkey("player_state");
    playerState.AddNumber("configured_level", GetConfig().Level());
    playerState.AddNumber("configured_xp", GetConfig().Xp());
    playerState.AddNumber("level", CSGOMaxPlayerLevel);
    playerState.AddNumber("xp", 4999);
    inventory.AddSubkey("items");

    std::string steamInf{ "ClientVersion=1569\nVersionDate=" };
    steamInf.append(versionDate.data(), versionDate.size());
    steamInf.push_back('\n');

    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && WriteTextFile("csgo/steam.inf", steamInf);
}

static bool SetPersistedPlayerProgress(int level, int xp)
{
    KeyValue inventory{ "inventory" };
    if (!inventory.ParseFromFile("csgo_gc/inventory.txt"))
    {
        return false;
    }

    KeyValue *playerState = inventory.GetSubkey("player_state");
    if (!playerState)
    {
        return false;
    }

    playerState->SetString("level", std::to_string(level));
    playerState->SetString("xp", std::to_string(xp));
    return inventory.WriteToFile("csgo_gc/inventory.txt");
}

static bool PersistedPlayerProgressIs(int level, int xp)
{
    KeyValue inventory{ "inventory" };
    if (!inventory.ParseFromFile("csgo_gc/inventory.txt"))
    {
        return false;
    }

    const KeyValue *playerState = inventory.GetSubkey("player_state");
    return playerState
        && playerState->GetNumber<int>("level") == level
        && playerState->GetNumber<int>("xp") == xp;
}

static bool PersistedItemHasDefIndex(uint64_t itemId, uint32_t defIndex)
{
    KeyValue inventory{ "inventory" };
    if (!inventory.ParseFromFile("csgo_gc/inventory.txt"))
    {
        return false;
    }

    const KeyValue *items = inventory.GetSubkey("items");
    if (!items)
    {
        return false;
    }

    const KeyValue *item = items->GetSubkey(std::to_string(itemId >> 32));
    return item && item->GetNumber<uint32_t>("def_index") == defIndex;
}

static bool RequestPrestigeInquiry(ClientGC &gc,
    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin &response)
{
    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin request;
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, request);

    EventData event;
    return WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, event)
        && ParseHostProtobuf(event, response);
}

static bool RequestPrestigeJob(ClientGC &gc,
    const CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin &request,
    uint64_t jobId,
    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin &response)
{
    EventData event;
    return SendGCProtobufJob(gc,
        k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin,
        request,
        jobId)
        && WaitForHostMessage(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin,
            event)
        && ParseHostJobProtobuf(event, jobId, response);
}

static bool ValidatePrestigeClaimEvents(const std::vector<EventData> &events,
    bool expectedCreate, uint32_t expectedDefIndex, uint64_t expectedItemId,
    uint64_t &actualItemId)
{
    size_t itemEventIndex = events.size();
    size_t responseEventIndex = events.size();
    bool foundPersona = false;
    bool foundMatchmakingHello = false;
    bool foundResponse = false;
    CMsgSOSingleObject itemData;
    CMsgSOSingleObject personaData;
    CSOEconItem item;
    CSOPersonaDataPublic persona;
    CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin response;

    for (size_t i = 0; i < events.size(); i++)
    {
        uint32_t type = static_cast<uint32_t>(events[i].id) & ~ProtobufMask;
        if (type == k_ESOMsg_Create || type == k_ESOMsg_Update)
        {
            CMsgSOSingleObject object;
            if (!ParseHostProtobuf(events[i], object))
            {
                return false;
            }

            if (object.type_id() == SOTypeItem)
            {
                uint32_t expectedType = expectedCreate
                    ? static_cast<uint32_t>(k_ESOMsg_Create)
                    : static_cast<uint32_t>(k_ESOMsg_Update);
                if (type != expectedType
                    || !item.ParseFromString(object.object_data()))
                {
                    return false;
                }
                itemData = object;
                itemEventIndex = i;
            }
            else if (object.type_id() == SOTypePersonaDataPublic)
            {
                if (!persona.ParseFromString(object.object_data()))
                {
                    return false;
                }
                personaData = object;
                foundPersona = true;
            }
        }
        else if (type == k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello)
        {
            CMsgGCCStrike15_v2_MatchmakingGC2ClientHello hello;
            foundMatchmakingHello = ParseHostProtobuf(events[i], hello)
                && hello.player_level() == 1
                && hello.player_cur_xp() == 0;
        }
        else if (type == k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin)
        {
            foundResponse = ParseHostProtobuf(events[i], response);
            responseEventIndex = i;
        }
    }

    actualItemId = item.id();
    return itemEventIndex < responseEventIndex
        && item.def_index() == expectedDefIndex
        && (!expectedItemId || item.id() == expectedItemId)
        && item.origin() == ItemOriginLevelUpReward
        && (!expectedCreate
            || item.inventory() == InventoryUnacknowledged(UnacknowledgedLevelUpReward))
        && foundPersona
        && persona.player_level() == 1
        && itemData.version() < personaData.version()
        && foundMatchmakingHello
        && foundResponse
        && response.defindex() == expectedDefIndex
        && response.upgradeid() == item.id()
        && response.prestigetime() != 0;
}

static bool ServiceMedalsFollowBuildYearAndPersistPrestige()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemovePrestigeFixtures();

    bool valid = WritePrestigeFixtures("Mar 28 2019");
    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin response;
        valid &= RequestPrestigeInquiry(gc, response)
            && response.defindex() == 1376
            && !response.has_upgradeid()
            && !response.has_prestigetime();

        constexpr uint64_t InquiryJobId = 101;
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin jobRequest;
        response.Clear();
        valid &= RequestPrestigeJob(gc, jobRequest, InquiryJobId, response)
            && response.defindex() == 1376
            && !response.has_upgradeid()
            && !response.has_prestigetime();

        constexpr uint64_t MalformedJobId = 102;
        constexpr uint8_t MalformedRequest[]{ 0x80 };
        EventData event;
        response.Clear();
        valid &= SendGCProtobufJobData(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin,
            MalformedRequest,
            sizeof(MalformedRequest),
            MalformedJobId)
            && WaitForHostMessage(gc,
                k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin,
                event)
            && ParseHostJobProtobuf(event, MalformedJobId, response)
            && response.ByteSizeLong() == 0;
    }

    RemovePrestigeFixtures();
    valid &= WritePrestigeFixtures("invalid");

    uint64_t medalItemId = 0;
    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin inquiry;
        valid &= RequestPrestigeInquiry(gc, inquiry)
            && inquiry.defindex() == 4873
            && !inquiry.has_upgradeid();

        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin staleClaim;
        staleClaim.set_defindex(4874);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, staleClaim);
        valid &= HostMessageNotReceived(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin);

        constexpr uint64_t StaleClaimJobId = 103;
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin rejectedResponse;
        valid &= RequestPrestigeJob(gc, staleClaim, StaleClaimJobId, rejectedResponse)
            && rejectedResponse.ByteSizeLong() == 0;

        inquiry.Clear();
        valid &= RequestPrestigeInquiry(gc, inquiry) && inquiry.defindex() == 4873;

        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin claim;
        claim.set_defindex(4873);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, claim);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, events)
            && ValidatePrestigeClaimEvents(events, true, 4873, 0, medalItemId);

        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin ineligibleInquiry;
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin,
            ineligibleInquiry);
        valid &= HostMessageNotReceived(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin);

        constexpr uint64_t IneligibleJobId = 104;
        rejectedResponse.Clear();
        valid &= RequestPrestigeJob(gc,
            ineligibleInquiry,
            IneligibleJobId,
            rejectedResponse)
            && rejectedResponse.ByteSizeLong() == 0;
    }

    valid &= medalItemId != 0
        && PersistedPlayerProgressIs(1, 0)
        && PersistedItemHasDefIndex(medalItemId, 4873);

    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin inquiry;
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, inquiry);
        valid &= HostMessageNotReceived(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin);

        CMsgClientHello hello;
        SendGCProtobuf(gc, k_EMsgGCClientHello, hello);
        EventData event;
        CMsgClientWelcome welcome;
        CMsgGCCStrike15_v2_MatchmakingGC2ClientHello matchmakingHello;
        valid &= WaitForHostMessage(gc, k_EMsgGCClientWelcome, event)
            && ParseHostProtobuf(event, welcome)
            && matchmakingHello.ParseFromString(welcome.game_data2())
            && matchmakingHello.player_level() == 1
            && matchmakingHello.player_cur_xp() == 0;
    }

    valid &= SetPersistedPlayerProgress(CSGOMaxPlayerLevel, 4999);
    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin inquiry;
        valid &= RequestPrestigeInquiry(gc, inquiry)
            && inquiry.defindex() == 4874
            && inquiry.upgradeid() == medalItemId;

        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin claim;
        claim.set_defindex(4874);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, claim);

        std::vector<EventData> events;
        uint64_t upgradedItemId = 0;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, events)
            && ValidatePrestigeClaimEvents(events, false, 4874, medalItemId, upgradedItemId)
            && upgradedItemId == medalItemId;
    }

    valid &= PersistedPlayerProgressIs(1, 0)
        && PersistedItemHasDefIndex(medalItemId, 4874)
        && SetPersistedPlayerProgress(CSGOMaxPlayerLevel, 4999);

    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin inquiry;
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin, inquiry);
        valid &= HostMessageNotReceived(gc,
            k_EMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin);

        constexpr uint64_t MissingPlanJobId = 105;
        CMsgGCCStrike15_v2_Client2GCRequestPrestigeCoin rejectedResponse;
        valid &= RequestPrestigeJob(gc,
            inquiry,
            MissingPlanJobId,
            rejectedResponse)
            && rejectedResponse.ByteSizeLong() == 0;
    }

    RemovePrestigeFixtures();
    return valid;
}

static void RemoveTournamentAccessFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/gc_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteTournamentAccessFixtures(std::initializer_list<uint32_t> inventoryDefIndexes)
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
    KeyValue &attributes = itemsGame.AddSubkey("attributes");
    auto addIntegerAttribute = [&](uint32_t defIndex, const char *name)
    {
        KeyValue &attribute = attributes.AddSubkey(std::to_string(defIndex));
        attribute.AddString("name", name);
        attribute.AddNumber("stored_as_integer", 1);
    };
    addIntegerAttribute(ItemSchema::AttributeStickerId0, "sticker slot 0 id");
    addIntegerAttribute(ItemSchema::AttributeTournamentEventId, "tournament event id");
    addIntegerAttribute(ItemSchema::AttributeCampaignId, "campaign id");
    addIntegerAttribute(ItemSchema::AttributeCampaignCompletionBitfield,
        "campaign completion bitfield");
    addIntegerAttribute(ItemSchema::AttributeOperationDropsAwardedPurchased,
        "operation drops awarded 1");
    addIntegerAttribute(ItemSchema::AttributeOperationDropsAwardedRedeemed,
        "operation drops awarded 0");

    KeyValue &prefabs = itemsGame.AddSubkey("prefabs");
    prefabs.AddSubkey("fan_token");
    prefabs.AddSubkey("fan_shield");

    KeyValue &passPrefab = prefabs.AddSubkey("paris_pass");
    passPrefab.AddString("prefab", "fan_token");
    KeyValue &passEvent = passPrefab.AddSubkey("attributes")
        .AddSubkey("tournament event id");
    passEvent.AddNumber("value", 21);

    KeyValue &journalPrefab = prefabs.AddSubkey("paris_journal");
    journalPrefab.AddString("prefab", "fan_shield");
    KeyValue &journalAttributes = journalPrefab.AddSubkey("attributes");
    journalAttributes.AddSubkey("tournament event id").AddNumber("value", 21);
    auto addGenerated = [&](const char *name, uint32_t value)
    {
        KeyValue &attribute = journalAttributes.AddSubkey(name);
        attribute.AddNumber("value", value);
        attribute.AddNumber("force_gc_to_generate", 1);
    };
    addGenerated("sticker slot 0 id", 6732);
    addGenerated("campaign id", 15);
    addGenerated("campaign completion bitfield", 1);
    addGenerated("operation drops awarded 1", 0);
    addGenerated("operation drops awarded 0", 0);

    KeyValue &items = itemsGame.AddSubkey("items");
    auto addItemDefinition = [&](uint32_t defIndex, const char *name, const char *prefab)
    {
        KeyValue &item = items.AddSubkey(std::to_string(defIndex));
        item.AddString("name", name);
        item.AddString("prefab", prefab);
    };
    addItemDefinition(100, "tournament_pass_paris2023", "paris_pass");
    addItemDefinition(101, "tournament_pass_paris2023_pack", "paris_pass");
    addItemDefinition(102, "tournament_pass_paris2023_charge", "paris_pass");
    addItemDefinition(200, "tournament_journal_paris2023", "paris_journal");

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &inventoryItems = inventory.AddSubkey("items");
    uint32_t highItemId = 1;
    for (uint32_t defIndex : inventoryDefIndexes)
    {
        KeyValue &item = inventoryItems.AddSubkey(std::to_string(highItemId++));
        item.AddNumber("def_index", defIndex);
        item.AddNumber("origin", ItemOriginPurchased);
    }

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt");
}

static uint64_t TournamentFixtureItemId(uint64_t steamId, uint32_t highItemId)
{
    return (static_cast<uint64_t>(highItemId) << 32) | (steamId & UINT32_MAX);
}

static bool ValidateTournamentActivationEvents(const std::vector<EventData> &events,
    uint64_t consumedItemId, uint32_t journalMessageType, uint32_t expectedTokens,
    uint64_t &journalItemId)
{
    size_t destroyIndex = events.size();
    size_t journalIndex = events.size();
    size_t notificationIndex = events.size();
    CSOEconItem journal;
    bool valid = true;

    for (size_t i = 0; i < events.size(); i++)
    {
        const uint32_t type = static_cast<uint32_t>(events[i].id) & ~ProtobufMask;
        if (type == k_ESOMsg_Destroy)
        {
            CMsgSOSingleObject object;
            CSOEconItem consumed;
            valid &= destroyIndex == events.size()
                && ParseHostProtobuf(events[i], object)
                && ParseItemObject(object, consumed)
                && consumed.id() == consumedItemId;
            destroyIndex = i;
        }
        else if (type == journalMessageType)
        {
            CMsgSOSingleObject object;
            valid &= journalIndex == events.size()
                && ParseHostProtobuf(events[i], object)
                && ParseItemObject(object, journal)
                && journal.def_index() == 200;
            journalIndex = i;
        }
        else if (type == k_EMsgGCItemCustomizationNotification)
        {
            CMsgGCItemCustomizationNotification notification;
            const bool parsed = ParseHostProtobuf(events[i], notification);
            valid &= notificationIndex == events.size()
                && parsed
                && notification.request()
                    == k_EGCItemCustomizationNotification_ActivateFanToken
                && notification.item_id_size() == 1;
            if (parsed && notification.item_id_size() == 1)
            {
                journalItemId = notification.item_id(0);
            }
            notificationIndex = i;
        }
    }

    uint32_t tokenCount = 0;
    valid &= destroyIndex < journalIndex
        && journalIndex < notificationIndex
        && journal.id() == journalItemId
        && GetUint32Attribute(journal,
            ItemSchema::AttributeOperationDropsAwardedPurchased, tokenCount)
        && tokenCount == expectedTokens;
    return valid;
}

static bool ViewerPassActivationCreatesAndPersistsJournal()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveTournamentAccessFixtures();
    if (!WriteTournamentAccessFixtures({ 100, 100 }))
    {
        RemoveTournamentAccessFixtures();
        return false;
    }

    const uint64_t passId = TournamentFixtureItemId(SteamId, 1);
    const uint64_t duplicatePassId = TournamentFixtureItemId(SteamId, 2);
    uint64_t journalId = 0;
    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgUseItem request;
        request.set_item_id(passId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, events)
            && ValidateTournamentActivationEvents(events, passId,
                k_ESOMsg_Create, 0, journalId);

        request.set_item_id(duplicatePassId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);
        valid &= HostMessageNotReceived(gc, k_EMsgGCItemCustomizationNotification);
    }

    {
        Inventory persisted{ SteamId };
        const CSOEconItem *journal = persisted.GetItem(journalId);
        uint32_t stickerId = 0;
        uint32_t campaignId = 0;
        uint32_t completion = 0;
        uint32_t purchased = 0;
        uint32_t redeemed = 0;
        valid &= !persisted.GetItem(passId)
            && persisted.GetItem(duplicatePassId)
            && journal
            && journal->origin() == ItemOriginPurchased
            && journal->inventory() == InventoryUnacknowledged(UnacknowledgedPurchased)
            && GetUint32Attribute(*journal, ItemSchema::AttributeStickerId0, stickerId)
            && stickerId == 6732
            && GetUint32Attribute(*journal, ItemSchema::AttributeCampaignId, campaignId)
            && campaignId == 15
            && GetUint32Attribute(*journal,
                ItemSchema::AttributeCampaignCompletionBitfield, completion)
            && completion == 1
            && GetUint32Attribute(*journal,
                ItemSchema::AttributeOperationDropsAwardedPurchased, purchased)
            && purchased == 0
            && GetUint32Attribute(*journal,
                ItemSchema::AttributeOperationDropsAwardedRedeemed, redeemed)
            && redeemed == 0;
    }

    RemoveTournamentAccessFixtures();
    return valid;
}

static bool ViewerPassTokenPacksUpdatePersistedJournal()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveTournamentAccessFixtures();
    if (!WriteTournamentAccessFixtures({ 101, 102 }))
    {
        RemoveTournamentAccessFixtures();
        return false;
    }

    const uint64_t packId = TournamentFixtureItemId(SteamId, 1);
    const uint64_t tokenId = TournamentFixtureItemId(SteamId, 2);
    uint64_t journalId = 0;
    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgUseItem request;
        request.set_item_id(packId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, events)
            && ValidateTournamentActivationEvents(events, packId,
                k_ESOMsg_Create, 3, journalId);

        request.set_item_id(tokenId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);
        events.clear();
        uint64_t updatedJournalId = 0;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, events)
            && ValidateTournamentActivationEvents(events, tokenId,
                k_ESOMsg_Update, 4, updatedJournalId)
            && updatedJournalId == journalId;
    }

    {
        Inventory persisted{ SteamId };
        const CSOEconItem *journal = persisted.GetItem(journalId);
        uint32_t purchased = 0;
        valid &= !persisted.GetItem(packId)
            && !persisted.GetItem(tokenId)
            && journal
            && GetUint32Attribute(*journal,
                ItemSchema::AttributeOperationDropsAwardedPurchased, purchased)
            && purchased == 4;
    }

    RemoveTournamentAccessFixtures();
    valid &= WriteTournamentAccessFixtures({ 102 });
    if (valid)
    {
        ClientGC gc{ SteamId };
        CMsgUseItem request;
        request.set_item_id(TournamentFixtureItemId(SteamId, 1));
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);
        valid &= HostMessageNotReceived(gc, k_EMsgGCItemCustomizationNotification);
    }
    {
        Inventory persisted{ SteamId };
        valid &= persisted.GetItem(TournamentFixtureItemId(SteamId, 1)) != nullptr;
    }

    RemoveTournamentAccessFixtures();
    return valid;
}

static bool SouvenirTokenInitializesMissingPurchasedCount()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveTournamentAccessFixtures();
    if (!WriteTournamentAccessFixtures({ 200, 102 }))
    {
        RemoveTournamentAccessFixtures();
        return false;
    }

    const uint64_t journalId = TournamentFixtureItemId(SteamId, 1);
    const uint64_t tokenId = TournamentFixtureItemId(SteamId, 2);
    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgUseItem request;
        request.set_item_id(tokenId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);

        std::vector<EventData> events;
        uint64_t updatedJournalId = 0;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, events)
            && ValidateTournamentActivationEvents(events, tokenId,
                k_ESOMsg_Update, 1, updatedJournalId)
            && updatedJournalId == journalId;
    }

    {
        Inventory persisted{ SteamId };
        const CSOEconItem *journal = persisted.GetItem(journalId);
        uint32_t purchased = 0;
        valid &= !persisted.GetItem(tokenId)
            && journal
            && GetUint32Attribute(*journal,
                ItemSchema::AttributeOperationDropsAwardedPurchased, purchased)
            && purchased == 1;
    }

    RemoveTournamentAccessFixtures();
    return valid;
}

static void RemoveSeasonalOperationFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/gc_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteSeasonalOperationFixtures()
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
    KeyValue &prefabs = itemsGame.AddSubkey("prefabs");
    KeyValue &seasonPass = prefabs.AddSubkey("season_pass");
    seasonPass.AddSubkey("tool").AddString("type", "season_pass");
    prefabs.AddSubkey("operation_coin");
    KeyValue &seasonCoin = prefabs.AddSubkey("season11_coin");
    seasonCoin.AddString("prefab", "operation_coin");
    seasonCoin.AddSubkey("attributes").AddNumber("season access", 10);

    KeyValue &items = itemsGame.AddSubkey("items");
    KeyValue &pass = items.AddSubkey("4758");
    pass.AddString("name", "CommunitySeasonEleven2021");
    pass.AddString("prefab", "season_pass");
    pass.AddSubkey("attributes").AddNumber("season access", 10);

    KeyValue &coin = items.AddSubkey("4759");
    coin.AddString("name", "CommunitySeasonEleven2021 Coin 1");
    coin.AddString("prefab", "season11_coin");
    coin.AddNumber("min_ilevel", 1);
    coin.AddNumber("max_ilevel", 1);

    KeyValue &operation = itemsGame.AddSubkey("seasonaloperations").AddSubkey("10");
    operation.AddSubkey("quest_mission_card").AddNumber("id", 9051);
    operation.AddSubkey("quest_mission_card").AddNumber("id", 9052);

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &inventoryItems = inventory.AddSubkey("items");
    for (uint32_t highItemId = 1; highItemId <= 2; highItemId++)
    {
        KeyValue &item = inventoryItems.AddSubkey(std::to_string(highItemId));
        item.AddNumber("def_index", 4758);
        item.AddNumber("origin", ItemOriginPurchased);
    }

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt");
}

static bool FindSeasonalOperation(const CMsgSOCacheSubscribed &subscription,
    uint32_t seasonValue, CSOAccountSeasonalOperation &result)
{
    for (const CMsgSOCacheSubscribed_SubscribedType &type : subscription.objects())
    {
        if (type.type_id() != SOTypeAccountSeasonalOperation)
        {
            continue;
        }

        for (const std::string &data : type.object_data())
        {
            CSOAccountSeasonalOperation operation;
            if (operation.ParseFromString(data) && operation.season_value() == seasonValue)
            {
                result = std::move(operation);
                return true;
            }
        }
    }

    return false;
}

static bool SeasonPassActivationCreatesAndPersistsOperationState()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveSeasonalOperationFixtures();
    if (!WriteSeasonalOperationFixtures())
    {
        RemoveSeasonalOperationFixtures();
        return false;
    }

    const uint64_t passId = TournamentFixtureItemId(SteamId, 1);
    const uint64_t duplicatePassId = TournamentFixtureItemId(SteamId, 2);
    uint64_t coinId = 0;
    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgUseItem request;
        request.set_item_id(passId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, events);

        size_t destroyIndex = events.size();
        size_t coinIndex = events.size();
        size_t operationIndex = events.size();
        size_t notificationIndex = events.size();
        CSOAccountSeasonalOperation operation;
        for (size_t i = 0; i < events.size(); i++)
        {
            const uint32_t type = static_cast<uint32_t>(events[i].id) & ~ProtobufMask;
            if (type == k_ESOMsg_Destroy)
            {
                CMsgSOSingleObject object;
                CSOEconItem destroyed;
                valid &= ParseHostProtobuf(events[i], object)
                    && ParseItemObject(object, destroyed)
                    && destroyed.id() == passId;
                destroyIndex = i;
            }
            else if (type == k_ESOMsg_Create)
            {
                CMsgSOSingleObject object;
                valid &= ParseHostProtobuf(events[i], object);
                if (object.type_id() == SOTypeItem)
                {
                    CSOEconItem coin;
                    valid &= coin.ParseFromString(object.object_data())
                        && coin.def_index() == 4759;
                    coinId = coin.id();
                    coinIndex = i;
                }
                else if (object.type_id() == SOTypeAccountSeasonalOperation)
                {
                    valid &= operation.ParseFromString(object.object_data());
                    operationIndex = i;
                }
            }
            else if (type == k_EMsgGCItemCustomizationNotification)
            {
                CMsgGCItemCustomizationNotification notification;
                valid &= ParseHostProtobuf(events[i], notification)
                    && notification.request()
                        == k_EGCItemCustomizationNotification_ActivateOperationCoin
                    && notification.item_id_size() == 1;
                if (notification.item_id_size() == 1)
                {
                    valid &= notification.item_id(0) == coinId;
                }
                notificationIndex = i;
            }
        }

        valid &= destroyIndex < coinIndex
            && coinIndex < operationIndex
            && operationIndex < notificationIndex
            && operation.season_value() == 10
            && operation.tier_unlocked() == 0
            && operation.premium_tiers() == 0
            && operation.mission_id() == 0
            && operation.missions_completed() == 0
            && operation.redeemable_balance() == 0
            && operation.season_pass_time() != 0;

        request.set_item_id(duplicatePassId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, request);
        valid &= HostMessageNotReceived(gc, k_EMsgGCItemCustomizationNotification);
    }

    {
        Inventory persisted{ SteamId };
        const CSOAccountSeasonalOperation *operation = persisted.GetSeasonalOperation(10);
        valid &= !persisted.GetItem(passId)
            && persisted.GetItem(duplicatePassId)
            && persisted.GetItem(coinId)
            && persisted.GetItem(coinId)->def_index() == 4759
            && operation
            && operation->season_value() == 10
            && operation->season_pass_time() != 0;

        CMsgSOCacheSubscribed clientSubscription;
        CMsgSOCacheSubscribed serverSubscription;
        persisted.BuildCacheSubscription(clientSubscription, false);
        persisted.BuildCacheSubscription(serverSubscription, true);
        CSOAccountSeasonalOperation clientOperation;
        CSOAccountSeasonalOperation serverOperation;
        valid &= FindSeasonalOperation(clientSubscription, 10, clientOperation)
            && FindSeasonalOperation(serverSubscription, 10, serverOperation)
            && clientOperation.SerializeAsString() == serverOperation.SerializeAsString();
    }

    RemoveSeasonalOperationFixtures();
    return valid;
}

static bool WaitForSeasonalOperationUpdate(ClientGC &gc, uint32_t seasonValue,
    uint32_t missionCardId, CSOAccountSeasonalOperation &clientOperation,
    CSOAccountSeasonalOperation &serverOperation)
{
    bool receivedClientUpdate = false;
    bool receivedServerUpdate = false;
    std::vector<EventData> events;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (std::chrono::steady_clock::now() < deadline)
    {
        gc.GetHostEvents(events);
        for (const EventData &event : events)
        {
            if (event.type != static_cast<int>(HostEvent::Message)
                && event.type != static_cast<int>(HostEvent::NetMessage))
            {
                continue;
            }

            GCMessageRead messageRead{
                0, event.buffer.data(), static_cast<uint32_t>(event.buffer.size())
            };
            if (!messageRead.IsValid() || !messageRead.IsProtobuf()
                || messageRead.TypeUnmasked() != k_ESOMsg_Update)
            {
                continue;
            }

            CMsgSOSingleObject update;
            CSOAccountSeasonalOperation operation;
            if (!messageRead.ReadProtobuf(update)
                || update.type_id() != SOTypeAccountSeasonalOperation
                || !operation.ParseFromString(update.object_data())
                || operation.season_value() != seasonValue
                || operation.mission_id() != missionCardId)
            {
                continue;
            }

            if (event.type == static_cast<int>(HostEvent::Message))
            {
                clientOperation = std::move(operation);
                receivedClientUpdate = true;
            }
            else
            {
                serverOperation = std::move(operation);
                receivedServerUpdate = true;
            }
        }

        if (receivedClientUpdate && receivedServerUpdate)
        {
            return true;
        }

        events.clear();
        std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
    }

    return false;
}

static bool SeasonalMissionCardSelectionValidatesAndPersists()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveSeasonalOperationFixtures();
    if (!WriteSeasonalOperationFixtures())
    {
        RemoveSeasonalOperationFixtures();
        return false;
    }

    const uint64_t passId = TournamentFixtureItemId(SteamId, 1);
    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgGCCstrike15_v2_ClientRequestNewMission missionRequest;
        missionRequest.set_campaign_id(10);
        missionRequest.set_mission_id(9051);
        SendGCProtobuf(gc,
            k_EMsgGCCStrike15_v2_ClientRequestNewMission, missionRequest);
        valid &= HostMessageOrNetMessageNotReceived(gc, k_ESOMsg_Update);

        CMsgUseItem passRequest;
        passRequest.set_item_id(passId);
        SendGCProtobuf(gc, k_EMsgGCUseItemRequest, passRequest);
        std::vector<EventData> activationEvents;
        valid &= WaitForHostMessagesUntil(gc,
            k_EMsgGCItemCustomizationNotification, activationEvents);

        missionRequest.set_campaign_id(9);
        SendGCProtobuf(gc,
            k_EMsgGCCStrike15_v2_ClientRequestNewMission, missionRequest);
        valid &= HostMessageOrNetMessageNotReceived(gc, k_ESOMsg_Update);

        missionRequest.set_campaign_id(10);
        missionRequest.set_mission_id(9999);
        SendGCProtobuf(gc,
            k_EMsgGCCStrike15_v2_ClientRequestNewMission, missionRequest);
        valid &= HostMessageOrNetMessageNotReceived(gc, k_ESOMsg_Update);

        missionRequest.set_mission_id(9052);
        SendGCProtobuf(gc,
            k_EMsgGCCStrike15_v2_ClientRequestNewMission, missionRequest);
        CSOAccountSeasonalOperation clientOperation;
        CSOAccountSeasonalOperation serverOperation;
        valid &= WaitForSeasonalOperationUpdate(gc, 10, 9052,
                clientOperation, serverOperation)
            && clientOperation.SerializeAsString() == serverOperation.SerializeAsString();

        SendGCProtobuf(gc,
            k_EMsgGCCStrike15_v2_ClientRequestNewMission, missionRequest);
        valid &= HostMessageOrNetMessageNotReceived(gc, k_ESOMsg_Update);
    }

    {
        Inventory persisted{ SteamId };
        const CSOAccountSeasonalOperation *operation = persisted.GetSeasonalOperation(10);
        valid &= operation && operation->mission_id() == 9052;

        CMsgSOCacheSubscribed clientSubscription;
        CMsgSOCacheSubscribed serverSubscription;
        persisted.BuildCacheSubscription(clientSubscription, false);
        persisted.BuildCacheSubscription(serverSubscription, true);
        CSOAccountSeasonalOperation clientOperation;
        CSOAccountSeasonalOperation serverOperation;
        valid &= FindSeasonalOperation(clientSubscription, 10, clientOperation)
            && FindSeasonalOperation(serverSubscription, 10, serverOperation)
            && clientOperation.mission_id() == 9052
            && clientOperation.SerializeAsString() == serverOperation.SerializeAsString();
    }

    RemoveSeasonalOperationFixtures();
    return valid;
}

static bool RequestEventFavorites(ClientGC &gc, bool allEvents, uint64_t jobId,
    std::string_view expectedFavorites)
{
    CMsgGCCStrike15_v2_GetEventFavorites_Request request;
    request.set_all_events(allEvents);
    if (!SendGCProtobufJob(gc,
        k_EMsgGCCStrike15_v2_GetEventFavorites_Request, request, jobId))
    {
        return false;
    }

    EventData event;
    CMsgGCCStrike15_v2_GetEventFavorites_Response response;
    return WaitForHostMessage(gc,
            k_EMsgGCCStrike15_v2_GetEventFavorites_Response, event)
        && ParseHostJobProtobuf(event, jobId, response)
        && response.all_events() == allEvents
        && response.json_favorites() == expectedFavorites
        && response.json_featured() == "[]";
}

static bool EventFavoritesPersistAndPreserveRequestJobs()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    constexpr const char *InventoryPath = "csgo_gc/inventory.txt";
    TestFilesystem::RemoveFile(InventoryPath);
    if (!TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        CMsgGCCStrike15_v2_SetEventFavorite favorite;
        favorite.set_eventid(21);
        favorite.set_is_favorite(true);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_SetEventFavorite, favorite);

        favorite.set_eventid(7);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_SetEventFavorite, favorite);

        valid &= RequestEventFavorites(gc, true, 7001, "[7,21]");
    }

    {
        ClientGC gc{ SteamId };
        valid &= RequestEventFavorites(gc, false, 7002, "[7,21]");

        CMsgGCCStrike15_v2_SetEventFavorite favorite;
        favorite.set_eventid(7);
        favorite.set_is_favorite(false);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_SetEventFavorite, favorite);
        valid &= RequestEventFavorites(gc, true, 7003, "[21]");
    }

    {
        Inventory persisted{ SteamId };
        valid &= persisted.EventFavorites() == std::set<uint64_t>{ 21 };
    }

    TestFilesystem::RemoveFile(InventoryPath);
    TestFilesystem::RemoveDirectory("csgo_gc");
    return valid;
}

static bool OfflineGCRequestsReceiveMinimalResponses()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    ClientGC gc{ SteamId };

    const uint32_t printCountBefore = Platform::g_printCount.load(std::memory_order_relaxed);
    bool valid = true;

    CMsgGCCStrike15_v2_MatchmakingStart matchmakingStart;
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_MatchmakingStart, matchmakingStart);

    CMsgGCCStrike15_v2_MatchmakingStop matchmakingStop;
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_MatchmakingStop, matchmakingStop);

    CMsgGCCStrike15_v2_Party_Register partyRegister;
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Party_Register, partyRegister);

    CMsgGCCStrike15_v2_MatchListRequestCurrentLiveGames emptyRequest;
    SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_Party_Unregister, emptyRequest);

    CMsgGCCStrike15_v2_Party_Search partySearch;
    partySearch.set_game_type(1234);
    constexpr uint64_t PartySearchJobId = 8101;
    valid &= SendGCProtobufJob(gc, k_EMsgGCCStrike15_v2_Party_Search,
        partySearch, PartySearchJobId);

    EventData event;
    CMsgGCCStrike15_v2_Party_SearchResults partySearchResponse;
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_Party_Search, event)
        && ParseHostJobProtobuf(event, PartySearchJobId, partySearchResponse)
        && partySearchResponse.entries_size() == 0;

    CMsgGCCStrike15_v2_Account_RequestCoPlays coPlays;
    auto *firstPlayer = coPlays.add_players();
    firstPlayer->set_accountid(123);
    firstPlayer->set_rtcoplay(456);
    auto *secondPlayer = coPlays.add_players();
    secondPlayer->set_accountid(789);
    constexpr uint64_t CoPlaysJobId = 8102;
    valid &= SendGCProtobufJob(gc, k_EMsgGCCStrike15_v2_Account_RequestCoPlays,
        coPlays, CoPlaysJobId);

    event = {};
    CMsgGCCStrike15_v2_Account_RequestCoPlays coPlaysResponse;
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_Account_RequestCoPlays, event)
        && ParseHostJobProtobuf(event, CoPlaysJobId, coPlaysResponse)
        && coPlaysResponse.players_size() == 2
        && coPlaysResponse.players(0).accountid() == 123
        && coPlaysResponse.players(0).rtcoplay() == 456
        && !coPlaysResponse.players(0).online()
        && coPlaysResponse.players(1).accountid() == 789
        && !coPlaysResponse.players(1).has_rtcoplay()
        && !coPlaysResponse.players(1).online()
        && coPlaysResponse.servertime() != 0;

    CMsgGCCStrike15_v2_AccountPrivacySettings privacySettings;
    constexpr uint64_t PrivacySettingsJobId = 8103;
    valid &= SendGCProtobufJob(gc, k_EMsgGCCStrike15_v2_AccountPrivacySettings,
        privacySettings, PrivacySettingsJobId);

    event = {};
    CMsgGCCStrike15_v2_AccountPrivacySettings privacySettingsResponse;
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_AccountPrivacySettings, event)
        && ParseHostJobProtobuf(event, PrivacySettingsJobId, privacySettingsResponse)
        && privacySettingsResponse.settings_size() == 0;

    auto *privacySetting = privacySettings.add_settings();
    privacySetting->set_setting_type(1);
    privacySetting->set_setting_value(3);
    constexpr uint64_t PrivacySettingsUpdateJobId = 8104;
    valid &= SendGCProtobufJob(gc, k_EMsgGCCStrike15_v2_AccountPrivacySettings,
        privacySettings, PrivacySettingsUpdateJobId);

    event = {};
    privacySettingsResponse.Clear();
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_AccountPrivacySettings, event)
        && ParseHostJobProtobuf(event, PrivacySettingsUpdateJobId, privacySettingsResponse)
        && privacySettingsResponse.settings_size() == 1
        && privacySettingsResponse.settings(0).setting_type() == 1
        && privacySettingsResponse.settings(0).setting_value() == 3;

    auto checkEmptyMatchList = [&gc, &event](uint32_t requestType,
        const void *requestData, uint32_t requestSize, uint64_t jobId)
    {
        if (!SendGCProtobufJobData(gc, requestType, requestData, requestSize, jobId))
        {
            return false;
        }

        event = {};
        CMsgGCCStrike15_v2_MatchList matchList;
        return WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_MatchList, event)
            && ParseHostJobProtobuf(event, jobId, matchList)
            && matchList.msgrequestid() == requestType
            && matchList.accountid() == static_cast<uint32_t>(SteamId)
            && matchList.servertime() != 0
            && matchList.matches_size() == 0;
    };

    valid &= checkEmptyMatchList(k_EMsgGCCStrike15_v2_MatchListRequestCurrentLiveGames,
        nullptr, 0, 8105);

    CMsgGCCStrike15_v2_MatchListRequestRecentUserGames recentGames;
    recentGames.set_accountid(static_cast<uint32_t>(SteamId));
    std::string recentGamesData;
    valid &= recentGames.SerializeToString(&recentGamesData)
        && checkEmptyMatchList(k_EMsgGCCStrike15_v2_MatchListRequestRecentUserGames,
            recentGamesData.data(), static_cast<uint32_t>(recentGamesData.size()), 8109);

    CMsgGCCStrike15_v2_MatchListRequestTournamentGames tournamentGames;
    tournamentGames.set_eventid(2023);
    std::string tournamentGamesData;
    valid &= tournamentGames.SerializeToString(&tournamentGamesData)
        && checkEmptyMatchList(k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames,
            tournamentGamesData.data(), static_cast<uint32_t>(tournamentGamesData.size()), 8106);

    constexpr uint64_t TournamentPredictionsJobId = 8107;
    valid &= SendGCProtobufJobData(gc, k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions,
            nullptr, 0, TournamentPredictionsJobId);

    event = {};
    CMsgGCCStrike15_v2_Predictions tournamentPredictions;
    valid &= WaitForHostMessage(gc, k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions, event)
        && ParseHostJobProtobuf(event, TournamentPredictionsJobId, tournamentPredictions)
        && !tournamentPredictions.has_event_id()
        && tournamentPredictions.group_match_team_picks_size() == 0;

    constexpr uint8_t TruncatedTournamentGamesRequest[] = { 0x80 };
    valid &= SendGCProtobufJobData(gc,
            k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames,
            TruncatedTournamentGamesRequest,
            sizeof(TruncatedTournamentGamesRequest),
            8108)
        && HostMessageNotReceived(gc, k_EMsgGCCStrike15_v2_MatchList);

    return valid
        && Platform::g_printCount.load(std::memory_order_relaxed) == printCountBefore;
}

// Gold trade-up fixture: one collection ("set_test") with two Covert AK skins,
// a case whose regular list contains that collection's skins and whose
// knife pool is a single Case Hardened Karambit.
static void RemoveGoldTradeUpFixtures()
{
    TestFilesystem::RemoveFile("csgo_gc/custom_cases.txt");
    TestFilesystem::RemoveFile("csgo_gc/price_sheet.txt");
    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveFile("csgo_gc/unusual_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo_gc/gc_loot_lists.txt");
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt");
    TestFilesystem::RemoveDirectory("csgo/scripts/items");
    TestFilesystem::RemoveDirectory("csgo/scripts");
    TestFilesystem::RemoveDirectory("csgo");
    TestFilesystem::RemoveDirectory("csgo_gc");
}

static bool WriteGoldTradeUpFixtures()
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

    KeyValue &attributes = itemsGame.AddSubkey("attributes");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeTexturePrefab)).AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeTextureSeed)).AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeTextureWear)).AddString("attribute_type", "float");
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeKillEater)).AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeKillEaterScoreType)).AddNumber("stored_as_integer", 1);
    attributes.AddSubkey(std::to_string(ItemSchema::AttributeTournamentEventId)).AddNumber("stored_as_integer", 1);

    KeyValue &items = itemsGame.AddSubkey("items");
    items.AddSubkey("7").AddString("name", "weapon_ak47");
    KeyValue &karambit = items.AddSubkey("507");
    karambit.AddString("name", "weapon_knife_karambit");
    karambit.AddString("item_quality", "unusual");
    karambit.AddString("item_rarity", "ancient");
    KeyValue &crate = items.AddSubkey("4001");
    crate.AddString("name", "crate_gold_test");
    crate.AddSubkey("tool").AddString("restriction", "gold_test");
    // real weapon cases point at their contents through a supply crate series
    crate.AddSubkey("attributes").AddSubkey("set supply crate series").AddNumber("value", 77);
    KeyValue &key = items.AddSubkey("4002");
    key.AddString("name", "key_gold_test");
    key.AddString("item_type", "weapon_case_key");
    key.AddSubkey("tool").AddString("restriction", "gold_test");

    KeyValue &paintKits = itemsGame.AddSubkey("paint_kits");
    KeyValue &defaultKit = paintKits.AddSubkey("0");
    defaultKit.AddString("name", "default");
    defaultKit.AddNumber("wear_remap_min", 0.0f);
    defaultKit.AddNumber("wear_remap_max", 1.0f);
    paintKits.AddSubkey("44").AddString("name", "aq_oiled");
    paintKits.AddSubkey("100").AddString("name", "covert_a");
    paintKits.AddSubkey("101").AddString("name", "covert_b");
    paintKits.AddSubkey("102").AddString("name", "classified_a");
    paintKits.AddSubkey("103").AddString("name", "consumer_a");

    KeyValue &paintRarities = itemsGame.AddSubkey("paint_kits_rarity");
    paintRarities.AddString("covert_a", "ancient");
    paintRarities.AddString("covert_b", "ancient");
    paintRarities.AddString("classified_a", "legendary");
    paintRarities.AddString("consumer_a", "common");

    KeyValue &set = itemsGame.AddSubkey("item_sets").AddSubkey("set_test");
    set.AddNumber("is_collection", 1);
    KeyValue &setItems = set.AddSubkey("items");
    setItems.AddNumber("[covert_a]weapon_ak47", 1);
    setItems.AddNumber("[covert_b]weapon_ak47", 1);
    setItems.AddNumber("[classified_a]weapon_ak47", 1);
    setItems.AddNumber("[consumer_a]weapon_ak47", 1);

    itemsGame.AddSubkey("revolving_loot_lists").AddString("77", "crate_gold_test");
    KeyValue &lootList = itemsGame.AddSubkey("client_loot_lists").AddSubkey("crate_gold_test");
    lootList.AddNumber("[covert_a]weapon_ak47", 1);
    lootList.AddNumber("[covert_b]weapon_ak47", 1);
    lootList.AddNumber("[consumer_a]weapon_ak47", 1);
    lootList.AddNumber("unusual_gold_test", 1);

    KeyValue unusualLootLists{ "unusual_loot_lists" };
    unusualLootLists.AddSubkey("unusual_gold_test").AddNumber("[aq_oiled]weapon_knife_karambit", 1);

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    KeyValue &inventoryItems = inventory.AddSubkey("items");

    auto addSkin = [&](uint32_t highItemId, uint32_t defIndex, uint32_t quality, uint32_t rarity,
        uint32_t paintKit, float wear)
    {
        KeyValue &item = inventoryItems.AddSubkey(std::to_string(highItemId));
        item.AddNumber("def_index", defIndex);
        item.AddNumber("quality", quality);
        item.AddNumber("origin", ItemOriginCrate);
        item.AddNumber("rarity", rarity);
        KeyValue &itemAttributes = item.AddSubkey("attributes");
        itemAttributes.AddNumber(std::to_string(ItemSchema::AttributeTexturePrefab), paintKit);
        itemAttributes.AddNumber(std::to_string(ItemSchema::AttributeTextureSeed), 1);
        itemAttributes.AddNumber(std::to_string(ItemSchema::AttributeTextureWear), wear);
    };

    // 1..5: Coverts, 6: Classified, 10..14: Karambits
    for (uint32_t i = 1; i <= 5; i++)
    {
        addSkin(i, 7, ItemSchema::QualityUnique, ItemSchema::RarityAncient, i % 2 ? 100 : 101, 0.2f);
    }
    addSkin(6, 7, ItemSchema::QualityUnique, ItemSchema::RarityLegendary, 102, 0.2f);
    for (uint32_t i = 10; i <= 14; i++)
    {
        addSkin(i, 507, ItemSchema::QualityUnusual, ItemSchema::RarityAncient, 44, 0.4f);
    }

    // 20..29: Classifieds for a souvenir contract, even ids are souvenirs
    for (uint32_t i = 20; i < 30; i++)
    {
        const bool souvenir = i % 2 == 0;
        addSkin(i, 7, souvenir ? ItemSchema::QualityTournament : ItemSchema::QualityUnique,
            ItemSchema::RarityLegendary, 102, 0.3f);
        if (souvenir)
        {
            inventoryItems.GetSubkey(std::to_string(i))->GetSubkey("attributes")
                ->AddNumber(std::to_string(ItemSchema::AttributeTournamentEventId), 19);
        }
    }

    // 100..249: cases for the pity test
    for (uint32_t i = 100; i < 250; i++)
    {
        KeyValue &crateItem = inventoryItems.AddSubkey(std::to_string(i));
        crateItem.AddNumber("def_index", 4001);
        crateItem.AddNumber("origin", ItemOriginDrop);
        crateItem.AddNumber("rarity", ItemSchema::RarityCommon);
    }

    KeyValue priceSheet{ "price_sheet" };
    KeyValue &store = priceSheet.AddSubkey("store");
    store.AddSubkey("store_banner_layout");
    KeyValue &templateEntry = store.AddSubkey("entries").AddSubkey("price_template");
    templateEntry.AddString("item_link", "weapon_ak47");
    templateEntry.AddSubkey("prices").AddNumber("USD", 249);

    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt")
        && priceSheet.WriteToFile("csgo_gc/price_sheet.txt");
}

struct CraftOutcome
{
    bool responded{};
    bool responseOk{};
    bool responseDenied{};
    int destroyCount{};
    bool created{};
    CSOEconItem createdItem;
};

static CraftOutcome SendCraft(ClientGC &gc, int16_t recipe, const std::vector<uint64_t> &itemIds)
{
    GCMessageWrite request{ k_EMsgGCCraft };
    request.WriteUint16(static_cast<uint16_t>(recipe));
    request.WriteUint16(static_cast<uint16_t>(itemIds.size()));
    for (uint64_t id : itemIds)
    {
        request.WriteUint64(id);
    }
    gc.PostToGC(GCEvent::Message, request.TypeMasked(), request.Data(), request.Size());

    CraftOutcome outcome;
    std::vector<EventData> events;
    if (!WaitForHostMessagesUntil(gc, k_EMsgGCCraftResponse, events))
    {
        return outcome;
    }

    constexpr size_t responseBodyOffset = sizeof(uint32_t) + sizeof(uint32_t)
        + sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint64_t);

    for (const EventData &event : events)
    {
        const uint32_t type = static_cast<uint32_t>(event.id) & ~ProtobufMask;
        if (type == k_ESOMsg_Create)
        {
            CMsgSOSingleObject object;
            outcome.created = ParseHostProtobuf(event, object) && ParseItemObject(object, outcome.createdItem);
        }
        else if (type == k_ESOMsg_Destroy)
        {
            outcome.destroyCount++;
        }
        else if (type == k_EMsgGCCraftResponse)
        {
            outcome.responded = true;
            outcome.responseOk = ValueAt(event.buffer.data(), responseBodyOffset + sizeof(int16_t),
                static_cast<uint32_t>(k_EGCMsgResponseOK));
            outcome.responseDenied = ValueAt(event.buffer.data(), responseBodyOffset + sizeof(int16_t),
                static_cast<uint32_t>(k_EGCMsgResponseDenied));
        }
    }

    return outcome;
}

static bool FindAttributeUint32(const CSOEconItem &item, uint32_t defIndex, uint32_t &value)
{
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        if (attribute.def_index() == defIndex && attribute.value_bytes().size() == sizeof(uint32_t))
        {
            std::memcpy(&value, attribute.value_bytes().data(), sizeof(value));
            return true;
        }
    }
    return false;
}

static bool FindAttributeFloat(const CSOEconItem &item, uint32_t defIndex, float &value)
{
    uint32_t bits = 0;
    if (!FindAttributeUint32(item, defIndex, bits))
    {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(value));
    return true;
}

static bool GoldTradeUpContractsProduceKnives()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };

        // a Classified mixed into Coverts is rejected and nothing is destroyed
        CraftOutcome mixed = SendCraft(gc, 5, { itemId(1), itemId(2), itemId(3), itemId(4), itemId(6) });
        valid &= mixed.responded && mixed.responseDenied
            && mixed.destroyCount == 0 && !mixed.created;

        // 5 Coverts -> Karambit
        CraftOutcome covert = SendCraft(gc, 5, { itemId(1), itemId(2), itemId(3), itemId(4), itemId(5) });
        float covertWear = -1.0f;
        uint32_t paintKit = 0;
        valid &= covert.responded && covert.responseOk
            && covert.destroyCount == 5 && covert.created
            && covert.createdItem.def_index() == 507
            && covert.createdItem.quality() == ItemSchema::QualityUnusual
            && covert.createdItem.rarity() == ItemSchema::RarityAncient
            && FindAttributeUint32(covert.createdItem, ItemSchema::AttributeTexturePrefab, paintKit) && paintKit == 44
            && FindAttributeFloat(covert.createdItem, ItemSchema::AttributeTextureWear, covertWear)
            && std::fabs(covertWear - 0.2f) < 0.0001f;

        // CS2 rule: souvenirs mix with normal skins and give a normal skin
        std::vector<uint64_t> souvenirInputs;
        for (uint32_t i = 20; i < 30; i++)
        {
            souvenirInputs.push_back(itemId(i));
        }
        CraftOutcome souvenir = SendCraft(gc, 4, souvenirInputs);
        valid &= souvenir.responded && souvenir.responseOk
            && souvenir.destroyCount == 10 && souvenir.created
            && souvenir.createdItem.def_index() == 7
            && souvenir.createdItem.quality() == ItemSchema::QualityUnique
            && souvenir.createdItem.rarity() == ItemSchema::RarityAncient
            && !HasAttribute(souvenir.createdItem, ItemSchema::AttributeTournamentEventId);

        // 5 Karambits at 0.4 -> Karambit at half the float
        CraftOutcome gold = SendCraft(gc, 5, { itemId(10), itemId(11), itemId(12), itemId(13), itemId(14) });
        float goldWear = -1.0f;
        valid &= gold.responded && gold.responseOk
            && gold.destroyCount == 5 && gold.created
            && gold.createdItem.def_index() == 507
            && gold.createdItem.quality() == ItemSchema::QualityUnusual
            && FindAttributeFloat(gold.createdItem, ItemSchema::AttributeTextureWear, goldWear)
            && std::fabs(goldWear - 0.2f) < 0.0001f;
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}


static bool CasePityGuaranteesGoldWithinLimit()
{
    // config.txt is absent in tests, so the defaults apply: soft pity 25, guaranteed 50
    const float base = 0.0001f;
    bool valid = CaseOpening::PityGoldChance(base, 0) == base
        && CaseOpening::PityGoldChance(base, 24) == base
        && CaseOpening::PityGoldChance(base, 30) > base
        && CaseOpening::PityGoldChance(base, 30) < CaseOpening::PityGoldChance(base, 40)
        && CaseOpening::PityGoldChance(base, 49) == 1.0f;

    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    {
        ClientGC gc{ SteamId };

        // the natural gold chance here is ~0.01%, so every gold below comes from pity
        int golds = 0;
        int sinceGold = 0;
        for (uint32_t crate = 100; crate < 250 && valid; crate++)
        {
            GCMessageWrite request{ k_EMsgGCUnlockCrate };
            request.WriteUint64(0);
            request.WriteUint64(itemId(crate));
            gc.PostToGC(GCEvent::Message, request.TypeMasked(), request.Data(), request.Size());

            std::vector<EventData> events;
            valid &= WaitForHostMessagesUntil(gc, k_EMsgGCItemCustomizationNotification, events);

            bool gotGold = false;
            for (const EventData &event : events)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                if ((static_cast<uint32_t>(event.id) & ~ProtobufMask) == k_ESOMsg_Create
                    && ParseHostProtobuf(event, object) && ParseItemObject(object, item))
                {
                    gotGold = item.quality() == ItemSchema::QualityUnusual;
                }
            }

            sinceGold++;
            if (gotGold)
            {
                golds++;
                sinceGold = 0;
            }
            valid &= sinceGold < 50;
        }

        // 150 cases with a 50 case guarantee must produce at least 3 golds
        valid &= golds >= 3;
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}

static bool MatchEndGivesOneDrop()
{
    constexpr uint64_t SteamId = 76561197960265729ull;

    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        gc.PostToGC(GCEvent::LocalPlayerMatchEnd, 0, nullptr, 0);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc, k_EMsgGCCStrike15_v2_GC2ClientTextMsg, events);

        int created = 0;
        for (const EventData &event : events)
        {
            CMsgSOSingleObject object;
            CSOEconItem item;
            if ((static_cast<uint32_t>(event.id) & ~ProtobufMask) == k_ESOMsg_Create
                && ParseHostProtobuf(event, object) && ParseItemObject(object, item))
            {
                created++;
                // either the case or one of the collection's skins, marked as a new drop
                valid &= item.origin() == ItemOriginDrop
                    && item.inventory() == InventoryUnacknowledged(UnacknowledgedDropped)
                    && (item.def_index() == 4001 || item.def_index() == 7);
            }
        }
        valid &= created == 1;

        // a repeated match end right after is ignored
        gc.PostToGC(GCEvent::LocalPlayerMatchEnd, 0, nullptr, 0);
        valid &= HostMessageNotReceived(gc, k_ESOMsg_Create);
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}


static bool StoreListsEveryWeaponCaseAndKey()
{
    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ 76561197960265729ull };

        CMsgStoreGetUserData storeData;
        storeData.set_price_sheet_version(0);
        SendGCProtobuf(gc, k_EMsgGCStoreGetUserData, storeData);

        EventData event;
        CMsgStoreGetUserDataResponse response;
        constexpr std::string_view CaseBanner{
            "\0" "4001\0" "\1" "market_link\0" "0\0",
            sizeof("\0" "4001\0" "\1" "market_link\0" "0\0") - 1
        };
        constexpr std::string_view CaseEntry{
            "\0" "crate_gold_test\0" "\1" "item_link\0" "crate_gold_test\0",
            sizeof("\0" "crate_gold_test\0" "\1" "item_link\0" "crate_gold_test\0") - 1
        };
        constexpr std::string_view KeyEntry{
            "\0" "key_gold_test\0" "\1" "item_link\0" "key_gold_test\0",
            sizeof("\0" "key_gold_test\0" "\1" "item_link\0" "key_gold_test\0") - 1
        };

        valid &= WaitForHostMessage(gc, k_EMsgGCStoreGetUserDataResponse, event)
            && ParseHostProtobuf(event, response)
            && response.result() == 1;
        const std::string &sheet = response.price_sheet();
        valid &= sheet.find(CaseBanner) != std::string::npos
            && sheet.find(CaseEntry) != std::string::npos
            && sheet.find(KeyEntry) != std::string::npos;
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}


static bool CustomCaseReplacesContents()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    auto itemId = [&](uint32_t highItemId)
    {
        return (static_cast<uint64_t>(highItemId) << 32) | (SteamId & UINT32_MAX);
    };

    RemoveGoldTradeUpFixtures();
    KeyValue customCases{ "custom_cases" };
    KeyValue &customCase = customCases.AddSubkey("4001");
    customCase.AddSubkey("items").AddString("[covert_b]weapon_ak47", "common");
    customCase.AddSubkey("golds").AddString("[covert_a]weapon_knife_karambit", "");
    if (!WriteGoldTradeUpFixtures() || !customCases.WriteToFile("csgo_gc/custom_cases.txt"))
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        for (uint32_t crate = 100; crate < 120 && valid; crate++)
        {
            GCMessageWrite request{ k_EMsgGCUnlockCrate };
            request.WriteUint64(0);
            request.WriteUint64(itemId(crate));
            gc.PostToGC(GCEvent::Message, request.TypeMasked(), request.Data(), request.Size());

            std::vector<EventData> events;
            valid &= WaitForHostMessagesUntil(gc, k_EMsgGCItemCustomizationNotification, events);

            int created = 0;
            for (const EventData &event : events)
            {
                CMsgSOSingleObject object;
                CSOEconItem item;
                uint32_t paintKit = 0;
                if ((static_cast<uint32_t>(event.id) & ~ProtobufMask) == k_ESOMsg_Create
                    && ParseHostProtobuf(event, object) && ParseItemObject(object, item))
                {
                    created++;
                    valid &= FindAttributeUint32(item, ItemSchema::AttributeTexturePrefab, paintKit);
                    const bool customSkin = item.def_index() == 7 && paintKit == 101
                        && item.rarity() == ItemSchema::RarityCommon;
                    const bool customGold = item.def_index() == 507 && paintKit == 100
                        && item.quality() == ItemSchema::QualityUnusual;
                    valid &= customSkin || customGold;
                }
            }
            valid &= created == 1;
        }
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}


static bool CustomCasePreviewPatchesOnlyTargetLists()
{
    const std::string original =
        "\"items_game\"\n"
        "{\n"
        "\t// a comment with { braces } and \"quotes\n"
        "\t\"revolving_loot_lists\" { \"1\" \"crate_one\" }\n"
        "\t\"client_loot_lists\"\n"
        "\t{\n"
        "\t\t\"crate_one\"\n"
        "\t\t{\n"
        "\t\t\t\"crate_one_rare\"\t\"1\"\n"
        "\t\t}\n"
        "\t\t\"crate_two\" { \"[x]weapon_ak47\" \"1\" }\n"
        "\t}\n"
        "\t\"other\" { \"crate_one\" { \"keep\" \"1\" } }\n"
        "}\n";

    GameSchemaPatch::LootListPreviews previews;
    previews["crate_one"] = { "[am_aqua_flecks]weapon_ak47", "[am_gold_brick]weapon_awp" };

    std::string text = original;
    bool valid = GameSchemaPatch::PatchLootLists(text, previews) == 1
        && text.find("\"[am_aqua_flecks]weapon_ak47\"") != std::string::npos
        && text.find("\"[am_gold_brick]weapon_awp\"") != std::string::npos
        && text.find("crate_one_rare") == std::string::npos
        && text.find("\"crate_two\" { \"[x]weapon_ak47\" \"1\" }") != std::string::npos
        && text.find("\"keep\" \"1\"") != std::string::npos
        && text.find("// a comment with { braces }") != std::string::npos;

    // the result still parses and has the new contents
    KeyValue parsed{ "root" };
    if (FILE *f = fopen("patched_schema_test.txt", "wb"))
    {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
    valid &= parsed.ParseFromFile("patched_schema_test.txt");
    TestFilesystem::RemoveFile("patched_schema_test.txt");
    const KeyValue *itemsGame = parsed.GetSubkey("items_game");
    const KeyValue *lists = itemsGame ? itemsGame->GetSubkey("client_loot_lists") : nullptr;
    const KeyValue *crateOne = lists ? lists->GetSubkey("crate_one") : nullptr;
    valid &= crateOne && crateOne->SubkeyCount() == 2;
    return valid;
}

static bool CustomCasePreviewBacksUpAndRestores()
{
    RemoveGoldTradeUpFixtures();
    TestFilesystem::RemoveFile(GameSchemaPatch::BackupPath);
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    auto readFile = [](const char *path)
    {
        std::string data;
        if (FILE *f = fopen(path, "rb"))
        {
            char buffer[4096];
            size_t read;
            while ((read = fread(buffer, 1, sizeof(buffer), f)) > 0)
            {
                data.append(buffer, read);
            }
            fclose(f);
        }
        return data;
    };

    const std::string original = readFile(GameSchemaPatch::SchemaPath);
    GameSchemaPatch::LootListPreviews previews;
    previews["crate_gold_test"] = { "[covert_b]weapon_ak47" };

    bool valid = !original.empty()
        && strcmp(GameSchemaPatch::OriginalSchemaPath(), GameSchemaPatch::SchemaPath) == 0
        && GameSchemaPatch::Apply(previews, {})
        && readFile(GameSchemaPatch::BackupPath) == original
        && readFile(GameSchemaPatch::SchemaPath) != original
        && strcmp(GameSchemaPatch::OriginalSchemaPath(), GameSchemaPatch::BackupPath) == 0
        && !GameSchemaPatch::Apply(previews, {}) // already up to date
        && GameSchemaPatch::Restore()
        && readFile(GameSchemaPatch::SchemaPath) == original
        && readFile(GameSchemaPatch::BackupPath).empty()
        && !GameSchemaPatch::Restore();

    TestFilesystem::RemoveFile(GameSchemaPatch::BackupPath);
    RemoveGoldTradeUpFixtures();
    return valid;
}


static bool GoldTradeUpRecipesAreAddedToSchema()
{
    const std::string original =
        "\"items_game\"\n"
        "{\n"
        "\t\"recipes\"\n"
        "\t{\n"
        "\t\t\"0\" { \"name\" \"#RT_MP_A\" }\n"
        "\t}\n"
        "\t\"other\" { \"recipes\" { } }\n"
        "}\n";

    std::string text = original;
    bool valid = GameSchemaPatch::InsertRecipes(text, GameSchemaPatch::GoldTradeUpRecipes);

    KeyValue parsed{ "root" };
    if (FILE *f = fopen("recipes_schema_test.txt", "wb"))
    {
        fwrite(text.data(), 1, text.size(), f);
        fclose(f);
    }
    valid &= parsed.ParseFromFile("recipes_schema_test.txt");
    TestFilesystem::RemoveFile("recipes_schema_test.txt");

    const KeyValue *itemsGame = parsed.GetSubkey("items_game");
    const KeyValue *recipes = itemsGame ? itemsGame->GetSubkey("recipes") : nullptr;
    const KeyValue *other = itemsGame ? itemsGame->GetSubkey("other") : nullptr;
    valid &= recipes && recipes->SubkeyCount() == 4
        && recipes->GetSubkey("0") && recipes->GetSubkey("5") && recipes->GetSubkey("15") && recipes->GetSubkey("6")
        && other && other->GetSubkey("recipes") && other->GetSubkey("recipes")->SubkeyCount() == 0;

    // covert recipe takes 5 Coverts
    const KeyValue *covert = recipes ? recipes->GetSubkey("5") : nullptr;
    const KeyValue *inputs = covert ? covert->GetSubkey("input_items") : nullptr;
    valid &= inputs && inputs->GetSubkey("5") && covert->GetString("do_B") == "#Exceedingly_Rare_Item";

    std::string noRecipes = "\"items_game\" { \"items\" { } }";
    valid &= !GameSchemaPatch::InsertRecipes(noRecipes, GameSchemaPatch::GoldTradeUpRecipes);
    return valid;
}

static bool PanoramaPatchLetsKnivesIntoContracts()
{
    // the two entries from the final CS:GO scripts/common/item_context_entries.js
    std::string source =
        "\t\t\tname: 'tradeup_add',\n"
        "\t\t\tAvailableForItem: function ( id ) {\n"
        "\t\t\t\tvar slot = ItemInfo.GetSlotSubPosition(id);\n"
        "\t\t\t\treturn slot && slot !==\"melee\" && slot !==\"c4\" && slot !==\"clothing_hands\" && !ItemInfo.IsEquippalbleButNotAWeapon(id) &&\n"
        "\t\t\t\t\t( InventoryAPI.CanTradeUp( id ) || InventoryAPI.GetNumItemsNeededToTradeUp( id ) > 0 );\n"
        "\t\t\tname: 'tradeup_remove',\n"
        "\t\t\t\treturn slot && slot !==\"melee\" && slot !==\"c4\" && slot !==\"clothing_hands\" && !ItemInfo.IsEquippalbleButNotAWeapon(id);\n"
        "\t\t\tname: 'equip_ct', AvailableForItem: function ( id ) { return slot !== \"melee\"; }\n";

    int changes = PanoramaHook::PatchItemContextEntries(source);
    bool valid = changes == 2
        && source.find("slot && slot !==\"c4\" &&\n") != std::string::npos
        && source.find("slot && slot !==\"c4\";") != std::string::npos
        && source.find("clothing_hands") == std::string::npos
        && source.find("return slot !== \"melee\";") != std::string::npos; // other entries untouched

    std::string unrelated = "var x = 1;";
    valid &= PanoramaHook::PatchItemContextEntries(unrelated) == 0 && unrelated == "var x = 1;";
    valid &= PanoramaHook::CaseStoreScript().find("var CsgoGcCaseStore") != std::string::npos
        && PanoramaHook::CaseStoreScript().find("SetData: _SetData") != std::string::npos;
    return valid;
}

static bool CaseStoreDataListsCasesWithKeys()
{
    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid;
    {
        ClientGC gc{ 76561197960265729ull };
        std::string data = PanoramaHook::BuildCaseStoreData(gc.GetItemSchema());
        valid = data.size() > 2 && data.front() == '[' && data.back() == ']'
            && data.find("[4001,4002,0,") != std::string::npos // crate, its key, weapon case
            && data.find("[4002,") == std::string::npos; // keys aren't listed on their own
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}

static void RemoveStarShopFixtures()
{
    TestFilesystem::RemoveFile("csgo/scripts/items/items_game.txt.csgo_gc_backup");
    RemoveSeasonalOperationFixtures();
}

// season 10 with its coin, and one tradable agent
static bool WriteStarShopFixtures()
{
    if (!WriteSeasonalOperationFixtures())
    {
        return false;
    }

    KeyValue schema{ "root" };
    if (!schema.ParseFromFile("csgo/scripts/items/items_game.txt"))
    {
        return false;
    }

    KeyValue &itemsGame = *schema.GetSubkey("items_game");
    itemsGame.GetSubkey("prefabs")->AddSubkey("customplayertradable");
    KeyValue &agent = itemsGame.GetSubkey("items")->AddSubkey("4619");
    agent.AddString("name", "customplayer_tm_test");
    agent.AddString("item_type", "customplayer");
    agent.AddString("prefab", "customplayertradable");

    // the player owns nothing, the operation appears with the first stars
    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    inventory.AddSubkey("items");
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt");
}

static bool StarShopPatchReplacesOnlyTheNewestOperationsGoods()
{
    std::string text =
        "\"items_game\"\r\n{\r\n"
        "\t\"seasonaloperations\"\r\n\t{\r\n"
        "\t\t\"9\"\r\n\t\t{\r\n"
        "\t\t\t\"operational_point_redeemable\"\r\n\t\t\t{\r\n\t\t\t\t\"points\"\t\t\"1\"\r\n\t\t\t\t\"item_name\"\t\t\"old9\"\r\n\t\t\t}\r\n"
        "\t\t}\r\n"
        "\t\t\"10\"\r\n\t\t{\r\n"
        "\t\t\t\"quest_mission_card\" { \"id\" \"9051\" }\r\n"
        "\t\t\t\"operational_point_redeemable\"\r\n\t\t\t{\r\n\t\t\t\t\"points\"\t\t\"2\"\r\n\t\t\t\t\"item_name\"\t\t\"old10a\"\r\n\t\t\t}\r\n"
        "\t\t\t\"operational_point_redeemable\"\r\n\t\t\t{\r\n\t\t\t\t\"points\"\t\t\"3\"\r\n\t\t\t\t\"item_name\"\t\t\"old10b\"\r\n\t\t\t}\r\n"
        "\t\t}\r\n"
        "\t}\r\n}\r\n";

    const std::string goods = StarShop::BuildRedeemablesText({
        { StarShop::GoodKind::Container, 10, 4001, "crate_new", 2 },
        { StarShop::GoodKind::Agent, 50, 0, "agent_pack", 1 },
    });

    std::string patched = text;
    const int removed = GameSchemaPatch::ReplaceRedeemables(patched, 10, goods);
    std::string missing = text;

    // the new goods come in order, the other season is untouched and the
    // surrounding blocks are still balanced
    const size_t crate = patched.find("crate_new");
    const size_t agent = patched.find("agent_pack");
    return removed == 2
        && GameSchemaPatch::ReplaceRedeemables(missing, 11, goods) == -1
        && missing == text
        && patched.find("old10") == std::string::npos
        && patched.find("old9") != std::string::npos
        && patched.find("9051") != std::string::npos
        && crate != std::string::npos && agent != std::string::npos && crate < agent
        && std::count(patched.begin(), patched.end(), '{') == std::count(patched.begin(), patched.end(), '}');
}

// a small schema with a few weapons and paint kits, loaded from fixtures
static bool WriteBotSkinFixtures()
{
    if (!TestFilesystem::MakeDirectory("csgo") || !TestFilesystem::MakeDirectory("csgo/scripts")
        || !TestFilesystem::MakeDirectory("csgo/scripts/items") || !TestFilesystem::MakeDirectory("csgo_gc"))
    {
        return false;
    }

    KeyValue schema{ "root" };
    KeyValue &itemsGame = schema.AddSubkey("items_game");
    KeyValue &paintKits = itemsGame.AddSubkey("paint_kits");
    KeyValue &defaultKit = paintKits.AddSubkey("0");
    defaultKit.AddString("name", "default");
    defaultKit.AddNumber("wear_remap_min", 0.0f);
    defaultKit.AddNumber("wear_remap_max", 1.0f);
    struct Kit { const char *name; int index; };
    for (const Kit &kit : { Kit{ "cu_ak47_cobra", 282 }, Kit{ "cu_fireserpent_ak47_bravo", 180 },
             Kit{ "am_ruby_marbleized", 415 }, Kit{ "hy_webs", 12 }, Kit{ "am_marble_fade", 413 },
             Kit{ "aq_blued", 42 }, Kit{ "hy_ak47lam_blue", 226 } })
    {
        KeyValue &key = paintKits.AddSubkey(std::to_string(kit.index));
        key.AddString("name", kit.name);
        key.AddNumber("wear_remap_min", 0.0f);
        key.AddNumber("wear_remap_max", 1.0f);
    }
    itemsGame.AddSubkey("items");

    KeyValue inventory{ "inventory" };
    inventory.AddNumber("format_version", 1);
    inventory.AddSubkey("items");
    KeyValue unusualLootLists{ "unusual_loot_lists" };
    KeyValue gcLootLists{ "gc_loot_lists" };
    return schema.WriteToFile("csgo/scripts/items/items_game.txt")
        && inventory.WriteToFile("csgo_gc/inventory.txt")
        && unusualLootLists.WriteToFile("csgo_gc/unusual_loot_lists.txt")
        && gcLootLists.WriteToFile("csgo_gc/gc_loot_lists.txt");
}

static bool BotSkinsPickThemedStableSkins()
{
    RemoveSeasonalOperationFixtures();
    if (!WriteBotSkinFixtures())
    {
        RemoveSeasonalOperationFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ 76561197960265729ull };
        BotSkins::Build(gc.GetItemSchema());
        valid &= BotSkins::IsBuilt();

        // a bot's pick doesn't change during a match
        BotSkins::Skin first{}, again{};
        valid &= BotSkins::Pick("weapon_ak47", 1234, first) && BotSkins::Pick("weapon_ak47", 1234, again)
            && first.paintKit == again.paintKit && first.seed == again.seed && first.wear == again.wear
            && (first.paintKit == 282 || first.paintKit == 180 || first.paintKit == 226)
            && first.seed < 1000 && first.wear >= 0.0f && first.wear <= 1.0f;

        // no skin for things that can't have one
        BotSkins::Skin none{};
        valid &= !BotSkins::Pick("weapon_hegrenade", 1234, none) && !BotSkins::Pick("weapon_c4", 1234, none);

        // only the plain starting knife is swapped, for one of the themed knives
        valid &= BotSkins::KnifeFor("weapon_ak47", 1234) == nullptr
            && BotSkins::KnifeFor("weapon_knife_karambit", 1234) == nullptr;
        const char *knife = BotSkins::KnifeFor("weapon_knife", 1234);
        valid &= knife && std::string_view{ knife }.rfind("weapon_", 0) == 0
            && std::string_view{ knife } != "weapon_knife"
            && BotSkins::KnifeFor("weapon_knife_t", 1234) == knife;
        BotSkins::Skin knifeSkin{};
        valid &= knife && BotSkins::Pick(knife, 1234, knifeSkin)
            && (knifeSkin.paintKit == 415 || knifeSkin.paintKit == 12 || knifeSkin.paintKit == 413
                || knifeSkin.paintKit == 42);

        // a new match can give different bots different themes: across many bots
        // more than one theme shows up
        std::unordered_set<std::string> themes;
        for (uint64_t bot = 1; bot <= 40; bot++)
        {
            themes.insert(BotSkins::ThemeName(bot * 7919));
        }
        valid &= themes.size() > 1;

        BotSkins::NewMatch();
        std::unordered_set<std::string> afterThemes;
        for (uint64_t bot = 1; bot <= 40; bot++)
        {
            afterThemes.insert(BotSkins::ThemeName(bot * 7919));
        }
        valid &= !afterThemes.empty();
    }

    RemoveSeasonalOperationFixtures();
    return valid;
}

static bool ViewmodelRangeIsScaledOnlyForConVarLikeMemory()
{
    // a fake ConVar: -2..2 with both limits set
    alignas(4) uint8_t convar[0x60] = {};
    convar[0x34] = 1;
    convar[0x3c] = 1;
    const float minimum = -2.0f, maximum = 2.0f;
    memcpy(convar + 0x38, &minimum, sizeof(minimum));
    memcpy(convar + 0x40, &maximum, sizeof(maximum));

    float scaledMin, scaledMax;
    const bool valid = ScaleViewmodelRange(convar, 10.0f);
    memcpy(&scaledMin, convar + 0x38, sizeof(scaledMin));
    memcpy(&scaledMax, convar + 0x40, sizeof(scaledMax));

    // memory without limits, or with nonsense limits, is left alone
    alignas(4) uint8_t other[0x60] = {};
    const float junkMin = 5.0f, junkMax = 9.0f;
    other[0x34] = 1;
    other[0x3c] = 1;
    memcpy(other + 0x38, &junkMin, sizeof(junkMin));
    memcpy(other + 0x40, &junkMax, sizeof(junkMax));
    uint8_t before[0x60];
    memcpy(before, other, sizeof(before));
    uint8_t noLimits[0x60] = {};

    return valid && scaledMin == -20.0f && scaledMax == 20.0f
        && !ScaleViewmodelRange(other, 10.0f) && memcmp(before, other, sizeof(before)) == 0
        && !ScaleViewmodelRange(noLimits, 10.0f) && !ScaleViewmodelRange(convar, 0.0f);
}

static bool CaseStoreListsCustomCasesFirstAndAgents()
{
    RemoveGoldTradeUpFixtures();
    KeyValue customCases{ "custom_cases" };
    KeyValue &customCase = customCases.AddSubkey("4001");
    customCase.AddString("name", "Test Custom Case");
    customCase.AddSubkey("items").AddString("[covert_b]weapon_ak47", "common");
    customCase.AddSubkey("golds").AddString("[covert_a]weapon_knife_karambit", "");
    if (!WriteGoldTradeUpFixtures() || !customCases.WriteToFile("csgo_gc/custom_cases.txt"))
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid;
    {
        ClientGC gc{ 76561197960265729ull };
        const std::string data = PanoramaHook::BuildCaseStoreData(gc.GetItemSchema());
        // kind 5 = custom case, listed first, with its name as the last field
        valid = data.find("[4001,4002,5,") == 1 && data.find(",\"Test Custom Case\"]") != std::string::npos;
    }

    TestFilesystem::RemoveFile("csgo_gc/custom_cases.txt");
    RemoveGoldTradeUpFixtures();
    return valid;
}

static bool StarShopSellsSkinsByRarity()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveGoldTradeUpFixtures();
    if (!WriteGoldTradeUpFixtures())
    {
        RemoveGoldTradeUpFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        const ItemSchema &schema = gc.GetItemSchema();
        StarShopConfig config;

        // a covert AK-47 skin from the fixture's loot lists
        std::vector<const LootListItem *> covert;
        valid &= schema.GetDropSkinCandidates(ItemSchema::RarityAncient, covert) && !covert.empty();
        Random random;
        CSOEconItem skin;
        valid &= valid && schema.CreateItemFromLootListItem(random, *covert.front(), false, ItemOriginDrop,
            UnacknowledgedDropped, skin);
        valid &= StarShop::SellValue(schema, config, skin) == 50;

        // other rarities pay their own price, a switched-off sell pays nothing
        CSOEconItem lower = skin;
        lower.set_rarity(ItemSchema::RarityRare);
        valid &= StarShop::SellValue(schema, config, lower) == 5;
        config.sellEnabled = false;
        valid &= StarShop::SellValue(schema, config, skin) == 0;
        config.sellEnabled = true;

        // a case and an item with no paint kit pay nothing
        CSOEconItem crate;
        crate.set_def_index(4001);
        CSOEconItem plain;
        plain.set_def_index(7); // weapon_ak47 with no skin
        plain.set_rarity(ItemSchema::RarityAncient);
        valid &= StarShop::SellValue(schema, config, crate) == 0 && StarShop::SellValue(schema, config, plain) == 0;

        // a knife is the unusual quality and pays the knife price
        CSOEconItem knife;
        knife.set_def_index(507);
        knife.set_quality(ItemSchema::QualityUnusual);
        valid &= StarShop::SellValue(schema, config, knife) == 150;
    }

    RemoveGoldTradeUpFixtures();
    return valid;
}

static bool StarShopMovesTheOperationScheduleToNow()
{
    const std::string text =
        "\"items_game\"\r\n{\r\n\t\"other\" { \"start\" \"5\" }\r\n"
        "\t\"quest_schedule\"\r\n\t{\r\n\t\t\"start\"\t\t\"1632254400\"\r\n"
        "\t\t\"length\"\t\t\"P9W\"\r\n\t\t\"slack\"\t\t\"P3W\"\r\n\t}\r\n}\r\n";

    const int64_t now = 1792000000;
    std::string moved = text;
    std::string running = text;
    const bool valid = GameSchemaPatch::ActivateOperation(moved, now) == 1
        && moved.find("\"1791913600\"") != std::string::npos // a day before now
        && moved.find("\"start\" \"5\"") != std::string::npos // other starts are left alone
        && moved.find("P9W") != std::string::npos
        && GameSchemaPatch::ActivateOperation(moved, now) == 0 // now inside the window
        && GameSchemaPatch::ActivateOperation(running, 1632254400 + 3600) == 0
        && running == text;

    std::string none = "\"items_game\"\n{\n}\n";
    return valid && GameSchemaPatch::ActivateOperation(none, now) == -1;
}

static bool StarShopEarnsStarsAndRedeemsARandomAgent()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    RemoveStarShopFixtures();
    if (!WriteStarShopFixtures())
    {
        RemoveStarShopFixtures();
        return false;
    }

    bool valid = true;
    {
        ClientGC gc{ SteamId };
        valid &= gc.GetItemSchema().LatestSeasonalOperation() == 10u
            && gc.GetItemSchema().GetAgentDefIndexes() == std::vector<uint32_t>{ 4619 };

        // 17 defuses at 3 stars each = 51 stars
        for (int i = 0; i < 17; i++)
        {
            uint32_t reason = static_cast<uint32_t>(StarShop::Reason::Defuse);
            gc.PostToGC(GCEvent::LocalPlayerStars, 0, &reason, sizeof(reason));
        }

        {
            CMsgGCCstrike15_v2_ClientRedeemMissionReward early;
            early.set_campaign_id(10);
            early.set_redeem_id(0);
            early.set_expected_cost(60); // wrong price, must be refused
            SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_ClientRedeemMissionReward, early);
        }

        CMsgGCCstrike15_v2_ClientRedeemMissionReward request;
        request.set_campaign_id(10);
        request.set_redeem_id(0);
        request.set_expected_cost(50);
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_ClientRedeemMissionReward, request);

        std::vector<EventData> events;
        valid &= WaitForHostMessagesUntil(gc, k_EMsgGCItemCustomizationNotification, events);

        bool agentCreated = false;
        uint32_t lastBalance = 0;
        for (const EventData &event : events)
        {
            const uint32_t type = static_cast<uint32_t>(event.id) & ~ProtobufMask;
            CMsgSOSingleObject object;
            if ((type != k_ESOMsg_Create && type != k_ESOMsg_Update) || !ParseHostProtobuf(event, object))
            {
                continue;
            }

            if (object.type_id() == SOTypeItem && type == k_ESOMsg_Create)
            {
                CSOEconItem item;
                if (item.ParseFromString(object.object_data()) && item.def_index() == 4619)
                {
                    agentCreated = true;
                }
            }
            else if (object.type_id() == SOTypeAccountSeasonalOperation)
            {
                CSOAccountSeasonalOperation operation;
                if (operation.ParseFromString(object.object_data()))
                {
                    lastBalance = operation.redeemable_balance();
                }
            }
        }

        valid &= agentCreated && lastBalance == 1;

        // one star left, a second purchase is refused
        SendGCProtobuf(gc, k_EMsgGCCStrike15_v2_ClientRedeemMissionReward, request);
        valid &= HostMessageNotReceived(gc, k_EMsgGCItemCustomizationNotification);
    }

    {
        Inventory persisted{ SteamId };
        bool hasAgent = false;
        for (const auto &pair : persisted.Items())
        {
            hasAgent |= pair.second.def_index() == 4619;
        }
        valid &= persisted.Stars(10) == 1 && hasAgent;
    }

    RemoveStarShopFixtures();
    return valid;
}

static bool TradeUpRecipesAcceptStatTrakWhenMixing()
{
    std::string text =
        "\"items_game\"\n"
        "{\n"
        "\t\"recipes\"\n"
        "\t{\n"
        "\t\t\"0\" { \"input_items\" { \"10\" { \"conditions\" {\n"
        "\t\t\t\"1\"\n\t\t\t{\n\t\t\t\t\"field\"\t\t\"*quality\"\n\t\t\t\t\"operator\"\t\t\"string==\"\n"
        "\t\t\t\t\"value\"\t\t\"unique\"\n\t\t\t\t\"required\"\t\t\"1\"\n\t\t\t}\n"
        "\t\t} } } }\n"
        "\t\t\"5\" { \"input_items\" { \"5\" { \"conditions\" {\n"
        "\t\t\t\"1\" { \"field\" \"*quality\" \"operator\" \"string==\" \"value\" \"unique\" \"required\" \"1\" }\n"
        "\t\t} } } }\n"
        "\t\t\"10\" { \"input_items\" { \"10\" { \"conditions\" {\n"
        "\t\t\t\"1\" { \"field\" \"*quality\" \"operator\" \"string==\" \"value\" \"strange\" \"required\" \"1\" }\n"
        "\t\t} } } }\n"
        "\t}\n"
        "\t\"other\" { \"field\" \"*quality\" \"operator\" \"string==\" \"value\" \"unique\" }\n"
        "}\n";

    size_t changed = GameSchemaPatch::RelaxTradeUpQuality(text);
    size_t unusual = 0;
    for (size_t pos = text.find("\"string!=\""); pos != std::string::npos; pos = text.find("\"string!=\"", pos + 1))
    {
        unusual++;
    }

    bool valid = changed == 2 && unusual == 2
        && text.find("\"value\" \"strange\"") != std::string::npos // StatTrak-only recipe untouched
        && text.find("\"other\" { \"field\" \"*quality\" \"operator\" \"string==\" \"value\" \"unique\" }") != std::string::npos;

    std::string noRecipes = "\"items_game\" { \"items\" { } }";
    valid &= GameSchemaPatch::RelaxTradeUpQuality(noRecipes) == 0;
    return valid;
}

static bool PityBoostsBetterRaritiesBeforeGold()
{
    // defaults: purple x3, pink x5, red x8 by the guaranteed case (50)
    auto near = [](float a, float b) { return std::fabs(a - b) < 0.001f; };
    return near(CaseOpening::PityRarityBoost(ItemSchema::RarityMythical, 0), 1.0f)
        && near(CaseOpening::PityRarityBoost(ItemSchema::RarityAncient, 25), 4.5f)
        && near(CaseOpening::PityRarityBoost(ItemSchema::RarityLegendary, 50), 5.0f)
        && near(CaseOpening::PityRarityBoost(ItemSchema::RarityAncient, 200), 8.0f)
        && near(CaseOpening::PityRarityBoost(ItemSchema::RarityRare, 40), 1.0f); // blues unchanged
}

int main()
{
    struct TestCase
    {
        const char *name;
        bool (*run)();
    };

    const TestCase tests[]{
        { "ExtendedCraftResponseSerialization", ExtendedCraftResponseSerialization },
        { "TruncatedCraftRequestGetsInvalidResponse", TruncatedCraftRequestGetsInvalidResponse },
        { "BasicStructHeaderSerializationIsUnchanged", BasicStructHeaderSerializationIsUnchanged },
        { "NetworkingClientRefreshesInterfacesAndSkipsIdlePolling",
            NetworkingClientRefreshesInterfacesAndSkipsIdlePolling },
        { "NetworkingServerPreservesHostEventSteamId",
            NetworkingServerPreservesHostEventSteamId },
        { "PlayerProfileRequestsReturnMinimalProfiles",
            PlayerProfileRequestsReturnMinimalProfiles },
        { "InventoryPersistenceProtectsFiles", InventoryPersistenceProtectsFiles },
        { "StatsSubscriptionDuplicatesKeepNewest", StatsSubscriptionDuplicatesKeepNewest },
        { "LoadoutStateTransitionsPreserveClassesAndSwapSlots",
            LoadoutStateTransitionsPreserveClassesAndSwapSlots },
        { "SOCacheVersionNegotiationAndRefresh", SOCacheVersionNegotiationAndRefresh },
        { "BaseItemCustomizationsPreserveRemainingState",
            BaseItemCustomizationsPreserveRemainingState },
        { "StatTrakSwapToolTwoPackCreatesTwoTools", StatTrakSwapToolTwoPackCreatesTwoTools },
        { "UnusualStatTrakKnivesCanSwapCounters", UnusualStatTrakKnivesCanSwapCounters },
        { "GoldTradeUpContractsProduceKnives", GoldTradeUpContractsProduceKnives },
        { "CasePityGuaranteesGoldWithinLimit", CasePityGuaranteesGoldWithinLimit },
        { "MatchEndGivesOneDrop", MatchEndGivesOneDrop },
        { "StoreListsEveryWeaponCaseAndKey", StoreListsEveryWeaponCaseAndKey },
        { "CustomCaseReplacesContents", CustomCaseReplacesContents },
        { "CustomCasePreviewPatchesOnlyTargetLists", CustomCasePreviewPatchesOnlyTargetLists },
        { "CustomCasePreviewBacksUpAndRestores", CustomCasePreviewBacksUpAndRestores },
        { "GoldTradeUpRecipesAreAddedToSchema", GoldTradeUpRecipesAreAddedToSchema },
        { "PanoramaPatchLetsKnivesIntoContracts", PanoramaPatchLetsKnivesIntoContracts },
        { "CaseStoreDataListsCasesWithKeys", CaseStoreDataListsCasesWithKeys },
        { "TradeUpRecipesAcceptStatTrakWhenMixing", TradeUpRecipesAcceptStatTrakWhenMixing },
        { "StarShopPatchReplacesOnlyTheNewestOperationsGoods",
            StarShopPatchReplacesOnlyTheNewestOperationsGoods },
        { "StarShopMovesTheOperationScheduleToNow", StarShopMovesTheOperationScheduleToNow },
        { "StarShopSellsSkinsByRarity", StarShopSellsSkinsByRarity },
        { "ViewmodelRangeIsScaledOnlyForConVarLikeMemory", ViewmodelRangeIsScaledOnlyForConVarLikeMemory },
        { "CaseStoreListsCustomCasesFirstAndAgents", CaseStoreListsCustomCasesFirstAndAgents },
        { "BotSkinsPickThemedStableSkins", BotSkinsPickThemedStableSkins },
        { "StarShopEarnsStarsAndRedeemsARandomAgent", StarShopEarnsStarsAndRedeemsARandomAgent },
        { "PityBoostsBetterRaritiesBeforeGold", PityBoostsBetterRaritiesBeforeGold },
        { "StorePurchasesFinalizeTransactionally", StorePurchasesFinalizeTransactionally },
        { "LegacyStorePriceSheetMatchesClientSchema",
            LegacyStorePriceSheetMatchesClientSchema },
        { "StatsSubscriptionPurchasesRejectDuplicates",
            StatsSubscriptionPurchasesRejectDuplicates },
        { "ServiceMedalsFollowBuildYearAndPersistPrestige",
            ServiceMedalsFollowBuildYearAndPersistPrestige },
        { "ViewerPassActivationCreatesAndPersistsJournal",
            ViewerPassActivationCreatesAndPersistsJournal },
        { "ViewerPassTokenPacksUpdatePersistedJournal",
            ViewerPassTokenPacksUpdatePersistedJournal },
        { "SouvenirTokenInitializesMissingPurchasedCount",
            SouvenirTokenInitializesMissingPurchasedCount },
        { "SeasonPassActivationCreatesAndPersistsOperationState",
            SeasonPassActivationCreatesAndPersistsOperationState },
        { "SeasonalMissionCardSelectionValidatesAndPersists",
            SeasonalMissionCardSelectionValidatesAndPersists },
        { "EventFavoritesPersistAndPreserveRequestJobs",
            EventFavoritesPersistAndPreserveRequestJobs },
        { "OfflineGCRequestsReceiveMinimalResponses",
            OfflineGCRequestsReceiveMinimalResponses },
    };

    bool allPassed = true;
    for (const TestCase &test : tests)
    {
        const bool passed = test.run();
        std::printf("%s: %s\n", test.name, passed ? "PASS" : "FAIL");
        allPassed &= passed;
    }

    return allPassed ? 0 : 1;
}
