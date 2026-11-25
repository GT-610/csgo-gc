#pragma once

#include "inventory_sync.h"

class ClientGC;
struct EventData;
struct AuthTicket
{
    uint64_t steamId{};
    std::vector<uint8_t> buffer;
};

class NetworkingClient
{
public:
    NetworkingClient(ISteamNetworkingMessages *networkingMessages);
    void SetNetworkingMessages(ISteamNetworkingMessages *networkingMessages);
    void Update(ClientGC *gc, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void SendMessage(const void *data, uint32_t size);
    void SendHostEvent(const EventData &event);
    void HandleIncomingMessage(ClientGC *gc, uint64_t steamId, const void *data, uint32_t size);
    void HandleSessionFailed(uint64_t steamId);
    void SetAuthTicket(uint32_t handle, const void *data, uint32_t size);
    void ClearAuthTicket(uint32_t handle);

private:
    bool HandleMessage(ClientGC *gc, uint64_t steamId, GCMessageRead &message);
    EResult SendRaw(const void *data, uint32_t size);
    void ClearPending();
    void RequestResync();
    void Pump(std::chrono::steady_clock::time_point now);

    ISteamNetworkingMessages *m_networkingMessages;
    uint64_t m_serverSteamId{};
    uint64_t m_generation{};
    InventorySync::SyncId m_syncId;
    std::unordered_set<uint64_t> m_retiredInstances;
    uint64_t m_revision{};
    std::queue<std::vector<uint8_t>> m_pending;
    size_t m_pendingBytes{};
    std::vector<uint8_t> m_inFlight;
    std::chrono::steady_clock::time_point m_nextSend{};
    uint32_t m_sendAttempts{};
    bool m_waitingSnapshot{};
    bool m_resync{};
    bool m_clearGCContext{};
    std::unordered_map<uint32_t, AuthTicket> m_tickets;

    STEAM_CALLBACK(NetworkingClient, OnSessionRequest,
        SteamNetworkingMessagesSessionRequest_t, m_sessionRequest);
    STEAM_CALLBACK(NetworkingClient, OnSessionFailed,
        SteamNetworkingMessagesSessionFailed_t, m_sessionFailed);
};
