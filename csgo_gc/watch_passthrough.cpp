#include "stdafx.h"
#include "watch_passthrough.h"
#include "config.h"
#include "gc_const.h"

#include <mutex>

namespace WatchPassthrough
{

namespace
{

struct Trampolines
{
    void *self{};
    SendFn send;
    AvailableFn available;
    RetrieveFn retrieve;
};

std::mutex s_mutex;
Trampolines s_trampolines;

// Large enough for a full tournament match list. Pump grows the buffer when the
// real GC reports a bigger frame.
constexpr uint32_t InitialBufferSize = 1u << 20;
constexpr uint32_t MaxBufferSize = 1u << 24;

uint32_t Masked(uint32_t type)
{
    return type | ProtobufMask;
}

uint32_t Unmasked(uint32_t type)
{
    return type & ~ProtobufMask;
}

bool IsForwardedType(uint32_t type)
{
    switch (Unmasked(type))
    {
    case k_EMsgGCCStrike15_v2_MatchListRequestTournamentGames:
    case k_EMsgGCCStrike15_v2_MatchListRequestFullGameInfo:
    case k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions:
        return true;

    default:
        return false;
    }
}

// Replies worth keeping. Everything else the real GC says is dropped, because the
// local GC already owns the inventory, store and matchmaking conversation.
bool IsKeptReply(uint32_t type)
{
    switch (Unmasked(type))
    {
    case k_EMsgGCCStrike15_v2_MatchList:
    case k_EMsgGCCStrike15_v2_GC2ClientTournamentInfo:
    case k_EMsgGCCStrike15_v2_MatchListRequestTournamentPredictions:
        return true;

    default:
        return false;
    }
}

// Rebinds the trampolines when the coordinator interface is a different object
// than the one they were recorded from.
void ResetIfOtherSelf(void *self)
{
    if (s_trampolines.self != self)
    {
        s_trampolines = Trampolines{};
        s_trampolines.self = self;
    }
}

// Rewrites the version field of a framed client hello while leaving the message
// header untouched, so the job id the client generated survives the trip.
bool RewriteHelloVersion(const void *data, uint32_t size, uint32_t version,
    std::vector<uint8_t> &out)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    if (size < sizeof(uint32_t) * 2)
    {
        return false;
    }

    uint32_t headerSize;
    memcpy(&headerSize, bytes + sizeof(uint32_t), sizeof(headerSize));

    const uint64_t bodyOffset = uint64_t{ sizeof(uint32_t) } + sizeof(headerSize) + headerSize;
    if (bodyOffset > size
        || uint64_t{ size } - bodyOffset > uint64_t{ std::numeric_limits<int>::max() })
    {
        return false;
    }

    CMsgClientHello hello;
    if (!hello.ParseFromArray(bytes + bodyOffset, static_cast<int>(size - bodyOffset)))
    {
        return false;
    }

    hello.set_version(version);

    out.assign(bytes, bytes + bodyOffset);
    const size_t bodyStart = out.size();
    out.resize(bodyStart + hello.ByteSizeLong());
    if (!hello.SerializeToArray(out.data() + bodyStart, static_cast<int>(out.size() - bodyStart)))
    {
        return false;
    }

    return true;
}

} // namespace

bool IsEnabled()
{
    return GetConfig().m_watchPassthrough;
}

uint32_t HelloVersionOverride()
{
    return GetConfig().m_watchPassthroughHelloVersion;
}

bool IsHello(uint32_t type)
{
    switch (Unmasked(type))
    {
    case k_EMsgGCClientHello:
    case k_EMsgGCClientHelloPartner:
    case k_EMsgGCClientHelloPW:
    case k_EMsgGCClientHelloR2:
    case k_EMsgGCClientHelloR3:
    case k_EMsgGCClientHelloR4:
        return true;

    default:
        return false;
    }
}

bool Forwards(uint32_t type)
{
    return IsForwardedType(type);
}

void AttachSend(void *self, const SendFn &send)
{
    std::lock_guard lock{ s_mutex };
    ResetIfOtherSelf(self);
    s_trampolines.send = send;
}

void AttachAvailable(void *self, const AvailableFn &available)
{
    std::lock_guard lock{ s_mutex };
    ResetIfOtherSelf(self);
    s_trampolines.available = available;
}

void AttachRetrieve(void *self, const RetrieveFn &retrieve)
{
    std::lock_guard lock{ s_mutex };
    ResetIfOtherSelf(self);
    s_trampolines.retrieve = retrieve;
}

void DetachAll()
{
    std::lock_guard lock{ s_mutex };
    s_trampolines = Trampolines{};
}

bool Forward(uint32_t type, const void *data, uint32_t size)
{
    SendFn send;
    {
        std::lock_guard lock{ s_mutex };
        send = s_trampolines.send;
    }

    if (!send)
    {
        return false;
    }

    std::vector<uint8_t> rewritten;
    const void *payload = data;
    uint32_t payloadSize = size;

    const uint32_t version = HelloVersionOverride();
    if (version && IsHello(type) && RewriteHelloVersion(data, size, version, rewritten))
    {
        payload = rewritten.data();
        payloadSize = static_cast<uint32_t>(rewritten.size());
    }

    return send(Masked(type), payload, payloadSize) == ResultOK;
}

void Pump(std::vector<std::pair<uint32_t, std::vector<uint8_t>>> &keep,
    std::vector<uint32_t> &droppedTypes)
{
    AvailableFn available;
    RetrieveFn retrieve;
    {
        std::lock_guard lock{ s_mutex };
        available = s_trampolines.available;
        retrieve = s_trampolines.retrieve;
    }

    if (!available || !retrieve)
    {
        return;
    }

    std::vector<uint8_t> buffer(InitialBufferSize);

    // Bounded so a misbehaving upstream cannot hold the callback for long.
    for (int i = 0; i < 64; i++)
    {
        uint32_t pendingSize = 0;
        if (!available(&pendingSize))
        {
            break;
        }

        if (pendingSize > buffer.size())
        {
            buffer.resize(pendingSize > MaxBufferSize ? MaxBufferSize : pendingSize);
        }

        uint32_t type = 0;
        uint32_t received = static_cast<uint32_t>(buffer.size());
        const int result = retrieve(&type, buffer.data(),
            static_cast<uint32_t>(buffer.size()), &received);
        if (result == ResultBufferTooSmall)
        {
            if (received > MaxBufferSize || received <= buffer.size())
            {
                break;
            }
            buffer.resize(received);
            continue;
        }

        if (result != ResultOK)
        {
            break;
        }

        if (IsKeptReply(type))
        {
            // The queue hands the type straight back to RetrieveMessage, and the
            // game matches the masked form the local GC already publishes.
            keep.emplace_back(type,
                std::vector<uint8_t>(buffer.begin(), buffer.begin() + received));
        }
        else
        {
            // Reported per type rather than counted: the useful diagnostic is
            // which conversation the upstream backend actually started, not how
            // many frames it took.
            if (std::find(droppedTypes.begin(), droppedTypes.end(), type) == droppedTypes.end())
            {
                droppedTypes.push_back(type);
            }
        }
    }
}

} // namespace WatchPassthrough
