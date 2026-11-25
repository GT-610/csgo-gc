#pragma once

#include "gc_message.h"
#include "networking_shared.h"

// Private GC-to-GC framing. Only the enclosed, unchanged Valve messages reach the game.
namespace InventorySync
{
constexpr uint32_t Magic = 0x534f4347; // GCOS, little endian
constexpr uint32_t Version = 1;
constexpr uint32_t ExtensionBytes = 20;
constexpr uint32_t MaxPacketBytes = 256 * 1024;
constexpr uint32_t MaxMessages = 64;
constexpr size_t MaxPendingBytes = 1024 * 1024;

struct SyncId
{
    uint64_t instance{};
    uint64_t generation{};
    bool operator==(const SyncId &) const = default;
    bool IsValid() const { return instance && generation; }
};

enum class ExtensionResult { Legacy, Supported, Unsupported, Malformed };

inline void WriteId(GCMessageWrite &message, SyncId id)
{
    message.WriteUint64(id.instance);
    message.WriteUint64(id.generation);
}

inline SyncId ReadId(GCMessageRead &message)
{
    SyncId id;
    id.instance = message.ReadUint64();
    id.generation = message.ReadUint64();
    return id;
}

inline void WriteConnectExtension(GCMessageWrite &message, SyncId id)
{
    message.WriteUint32(Magic);
    message.WriteUint32(ExtensionBytes);
    message.WriteUint32(Version);
    WriteId(message, id);
}

inline ExtensionResult ReadConnectExtension(GCMessageRead &message, SyncId &id)
{
    id = {};
    if (!message.IsValid()) return ExtensionResult::Malformed;
    if (!message.Remaining()) return ExtensionResult::Legacy;
    if (message.Remaining() < 12 || message.ReadUint32() != Magic)
        return ExtensionResult::Malformed;
    uint32_t length = message.ReadUint32();
    if (length < 4 || length > 1024 || length != message.Remaining())
        return ExtensionResult::Malformed;
    uint32_t version = message.ReadUint32();
    if (version != Version) return ExtensionResult::Unsupported;
    if (length != ExtensionBytes) return ExtensionResult::Malformed;
    id = ReadId(message);
    return message.IsValid() && id.IsValid() && !message.Remaining()
        ? ExtensionResult::Supported : ExtensionResult::Malformed;
}

inline bool IsSOCacheMessage(uint32_t type)
{
    return type == k_ESOMsg_CacheSubscribed || type == k_ESOMsg_Create
        || type == k_ESOMsg_Update || type == k_ESOMsg_Destroy
        || type == k_ESOMsg_UpdateMultiple;
}

struct Batch
{
    SyncId id;
    uint64_t baseRevision{};
    uint64_t revision{};
    bool full{};
    std::vector<std::vector<uint8_t>> messages;
};

inline GCMessageWrite BuildBatch(const Batch &batch)
{
    GCMessageWrite message{ k_EMsgNetworkInventoryBatch };
    WriteId(message, batch.id);
    message.WriteUint64(batch.baseRevision);
    message.WriteUint64(batch.revision);
    message.WriteUint32(batch.full ? 1 : 0);
    message.WriteUint32(static_cast<uint32_t>(batch.messages.size()));
    for (const auto &payload : batch.messages)
    {
        message.WriteUint32(static_cast<uint32_t>(payload.size()));
        message.WriteData(payload.data(), static_cast<uint32_t>(payload.size()));
    }
    return message;
}

inline bool ReadBatch(GCMessageRead &message, Batch &batch)
{
    batch = {};
    if (!message.IsValid() || message.IsProtobuf()
        || message.TypeUnmasked() != k_EMsgNetworkInventoryBatch
        || message.Remaining() > MaxPacketBytes - 18) return false;
    batch.id = ReadId(message);
    batch.baseRevision = message.ReadUint64();
    batch.revision = message.ReadUint64();
    uint32_t kind = message.ReadUint32();
    uint32_t count = message.ReadUint32();
    if (!message.IsValid() || !batch.id.IsValid() || kind > 1 || !count || count > MaxMessages)
        return false;
    batch.full = kind == 1;
    if (batch.full ? (batch.baseRevision || batch.revision != 1 || count != 1)
        : (!batch.baseRevision || batch.baseRevision == UINT64_MAX
            || batch.revision != batch.baseRevision + 1)) return false;
    for (uint32_t i = 0; i < count; ++i)
    {
        uint32_t size = message.ReadUint32();
        if (!message.IsValid() || !size || size > message.Remaining()) return false;
        const auto *data = static_cast<const uint8_t *>(message.ReadData(size));
        GCMessageRead inner{ 0, data, size };
        if (!inner.IsValid() || !inner.IsProtobuf() || !IsSOCacheMessage(inner.TypeUnmasked())
            || ((inner.TypeUnmasked() == k_ESOMsg_CacheSubscribed) != batch.full)) return false;
        batch.messages.emplace_back(data, data + size);
    }
    return message.IsValid() && !message.Remaining();
}

struct Status
{
    SyncId id;
    uint64_t revision{};
    bool ready{};
};

inline GCMessageWrite BuildStatus(const Status &status)
{
    GCMessageWrite message{ k_EMsgNetworkInventoryStatus };
    WriteId(message, status.id);
    message.WriteUint64(status.revision);
    message.WriteUint32(status.ready ? 1 : 0);
    return message;
}

inline bool ReadStatus(GCMessageRead &message, Status &status)
{
    if (!message.IsValid() || message.IsProtobuf()
        || message.TypeUnmasked() != k_EMsgNetworkInventoryStatus || message.Remaining() != 28)
        return false;
    status.id = ReadId(message);
    status.revision = message.ReadUint64();
    uint32_t ready = message.ReadUint32();
    status.ready = ready == 1;
    return message.IsValid() && status.id.IsValid() && ready <= 1
        && (status.ready == (status.revision != 0));
}

inline GCMessageWrite BuildResyncRequest(SyncId id)
{
    GCMessageWrite message{ k_EMsgNetworkInventoryResync };
    WriteId(message, id);
    return message;
}

inline bool ReadResyncRequest(GCMessageRead &message, SyncId &id)
{
    if (!message.IsValid() || message.IsProtobuf()
        || message.TypeUnmasked() != k_EMsgNetworkInventoryResync || message.Remaining() != 16)
        return false;
    id = ReadId(message);
    return message.IsValid() && id.IsValid();
}
}
