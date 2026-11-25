#pragma once

#include "inventory_sync.h"

struct EventData;
class SharedGC;

// Main-thread transport and commit state. ServerGC validates bodies on its worker;
// only DeliverValidatedEvent may advance the cache revision and acknowledge it.
class NetworkingServer
{
public:
    NetworkingServer(ISteamNetworkingMessages *networkingMessages);
    bool ReceiveMessage(SteamNetworkingMessage_t *&message);
    uint64_t Generation(uint64_t steamId) const;
    void ClientConnected(uint64_t steamId, const void *ticket, uint32_t ticketSize);
    void ClientDisconnected(uint64_t steamId);
    void Update(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // Returns false for internal/dropped messages; true means send to ServerGC.
    bool HandleIncomingMessage(uint64_t steamId, const void *data, uint32_t size);
    void HandleSessionRequest(uint64_t steamId);
    void HandleSessionFailed(uint64_t steamId);
    void ResetSOCache(uint64_t steamId = 0);
    void RejectSync(const EventData &event);
    bool DeliverValidatedEvent(const EventData &event,
        const std::function<void(uint32_t, std::vector<uint8_t> &&)> &deliver);

    void SendMessage(uint64_t steamId, const void *data, uint32_t size);
    void SendHostEvent(const EventData &event);

private:
    enum class Protocol { Unknown, Legacy, Versioned };
    struct ClientSession
    {
        std::vector<uint8_t> ticket;
        uint32_t connectAttempts{};
        std::chrono::steady_clock::time_point nextConnect{};
        std::chrono::steady_clock::time_point nextStatus{};
        std::chrono::steady_clock::time_point nextResync{};
        InventorySync::SyncId syncId;
        uint64_t revision{};
        Protocol protocol{};
        bool hasSOCache{};
        bool legacySnapshotReceived{};
    };

    void Restart(ClientSession &session);
    void SendStatus(uint64_t steamId, ClientSession &session,
        std::chrono::steady_clock::time_point now);
    void SendConnectMessage(uint64_t steamId, ClientSession &session,
        std::chrono::steady_clock::time_point now);

    ISteamNetworkingMessages *const m_networkingMessages;
    const uint64_t m_instance;
    uint64_t m_generation{};
    std::unordered_map<uint64_t, ClientSession> m_clients;

    STEAM_GAMESERVER_CALLBACK(NetworkingServer, OnSessionRequest,
        SteamNetworkingMessagesSessionRequest_t, m_sessionRequest);
    STEAM_GAMESERVER_CALLBACK(NetworkingServer, OnSessionFailed,
        SteamNetworkingMessagesSessionFailed_t, m_sessionFailed);
};
