#include "stdafx.h"
#include "gc_message.h"

#include <cstring>

namespace Platform
{

void Print(const char *, ...)
{
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

    GCMessageWrite message{ k_EMsgGCCraftResponse, GCMessageWrite::StructHeader::Extended };
    message.WriteUint16(static_cast<uint16_t>(responseIndex));
    message.WriteUint32(k_EGCMsgResponseOK);
    message.WriteUint16(1);
    message.WriteUint64(craftedItemId);

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

int main()
{
    return ExtendedCraftResponseSerialization()
        && BasicStructHeaderSerializationIsUnchanged() ? 0 : 1;
}
