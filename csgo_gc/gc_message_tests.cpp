#include "stdafx.h"
#include "gc_client.h"
#include "gc_message.h"
#include "keyvalue.h"

#include <cstring>
#include <filesystem>
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

    std::error_code error;
    std::filesystem::create_directory(InventoryDirectory, error);
    if (error)
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
    std::filesystem::remove(InventoryPath, error);

    {
        Inventory inventory{ 76561197960265729ull };
    }

    KeyValue savedInventory{ "inventory" };
    bool validEmptyInventory = savedInventory.ParseFromFile(InventoryPath)
        && savedInventory.GetNumber<int>("format_version") == 1;

    error.clear();
    std::filesystem::remove(InventoryPath, error);
    error.clear();
    std::filesystem::remove(InventoryDirectory, error);
    return preserved && validEmptyInventory;
}

int main()
{
    return ExtendedCraftResponseSerialization()
        && TruncatedCraftRequestGetsInvalidResponse()
        && BasicStructHeaderSerializationIsUnchanged()
        && InventoryPersistenceProtectsFiles() ? 0 : 1;
}
