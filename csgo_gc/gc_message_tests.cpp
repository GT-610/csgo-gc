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
    inventory.AddSubkey("default_equips");
    return inventory.WriteToFile("csgo_gc/inventory.txt");
}

static bool LoadoutStateTransitionsPreserveClassesAndSwapSlots()
{
    constexpr uint64_t SteamId = 76561197960265729ull;
    constexpr uint64_t Item1 = (uint64_t{ 1 } << 32) | (SteamId & UINT32_MAX);
    constexpr uint64_t Item2 = (uint64_t{ 2 } << 32) | (SteamId & UINT32_MAX);

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    if (!WriteLoadoutFixture())
    {
        return false;
    }

    bool valid = true;
    {
        Inventory inventory{ SteamId };

        CMsgSOMultipleObjects unequip;
        valid &= inventory.EquipItem(Item1, 2, 0xffff, false, unequip);
        const CSOEconItem *item1 = inventory.GetItem(Item1);
        valid &= item1 && EquippedSlotForClass(*item1, 2) == -1
            && EquippedSlotForClass(*item1, 3) == 1;

        CMsgSOMultipleObjects reequip;
        valid &= inventory.EquipItem(Item1, 2, 1, false, reequip);

        CMsgSOMultipleObjects swap;
        valid &= inventory.EquipItem(Item1, 2, 2, true, swap);
        item1 = inventory.GetItem(Item1);
        const CSOEconItem *item2 = inventory.GetItem(Item2);
        valid &= item1 && item2
            && EquippedSlotForClass(*item1, 2) == 2
            && EquippedSlotForClass(*item1, 3) == 1
            && EquippedSlotForClass(*item2, 2) == 1;

        CMsgSOMultipleObjects move;
        valid &= inventory.EquipItem(Item1, 2, 3, false, move);
        item1 = inventory.GetItem(Item1);
        valid &= item1 && EquippedSlotForClass(*item1, 2) == 3
            && EquippedSlotForClass(*item1, 3) == 1;
    }

    TestFilesystem::RemoveFile("csgo_gc/inventory.txt");
    TestFilesystem::RemoveDirectory("csgo_gc");
    return valid;
}

int main()
{
    return ExtendedCraftResponseSerialization()
        && TruncatedCraftRequestGetsInvalidResponse()
        && BasicStructHeaderSerializationIsUnchanged()
        && InventoryPersistenceProtectsFiles()
        && LoadoutStateTransitionsPreserveClassesAndSwapSlots() ? 0 : 1;
}
