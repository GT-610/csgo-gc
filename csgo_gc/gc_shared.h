#pragma once

#include "gc_message.h"

enum class HostEvent
{
    Message, // id contains the message type, buffer contains the payload
    NetMessage, // id contains the recipient steam id, buffer contains the payload
    MicroTransactionResponse, // id contains the Steam microtransaction order id
    ValidatedNetMessage, // id is peer; generation binds validation to a live session
    InventorySyncRejected, // validated framing, rejected SOCache body; id is peer
    ResetSOCaches, // game GC lifecycle or subscription refresh; id 0 means all peers
};

enum class GCEvent
{
    Message, // id contains the message type, buffer contains the payload
    NetMessage, // id contains the recipient steam id, buffer contains the payload
    SOCacheRequest, // sent to client gc when connected to a gameserver
    LocalPlayerRoundMVP, // sent to client gc when the local player earns round MVP
    SyncLocalPlayerMusicKitState, // sent to client gc from the main thread, buffer contains the local userid
    ClientSOCacheUnsubscribe, // sent to server gc when a client disconnects, id contains the steam id
    RconCommand, // sent to client gc, buffer contains an owned RconCommand holder pointer
    NetMessageCommitted, // server inventory accepted into the host queue
};

struct EventData
{
    int type; // HostEvent or GCEvent
    uint64_t id;
    std::vector<uint8_t> buffer;
    uint64_t generation{}; // local routing generation, never inferred from SteamID alone
};

// shared logic between client and server gcs
class SharedGC
{
public:
    void PostToGC(GCEvent type, uint64_t id, const void *data, uint32_t dataSize,
        uint64_t generation = 0);

    void GetHostEvents(std::vector<EventData> &events);

protected:
    // must be manually called by the gc
    void StartThread();
    void StopThread();

    void PostToHost(HostEvent type, uint64_t id, const void *data, uint32_t dataSize,
        uint64_t generation = 0);
    uint64_t EventGeneration() const { return m_eventGeneration; }

private:
    uint64_t m_eventGeneration{}; // only accessed on the GC worker
    virtual void HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer) = 0;

    void WorkerThread();

    std::mutex m_gcEventMutex;
    std::condition_variable m_cv;
    std::vector<EventData> m_gcEvents;
    std::thread m_thread;
    bool m_stopping{ false };

    std::mutex m_hostEventMutex;
    std::vector<EventData> m_hostEvents;
};

const char *MessageName(uint32_t type);
