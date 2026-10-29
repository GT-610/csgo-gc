#pragma once

#include "networking_shared.h"

class GCMessageWrite;
struct EventData;

class NetworkingServer
{
public:
    NetworkingServer(ISteamNetworkingMessages *networkingMessages);

    // caller need to call message->Release() because fuck you
    bool ReceiveMessage(SteamNetworkingMessage_t *&message);

    void ClientConnected(uint64_t steamId, const void *ticket, uint32_t ticketSize);
    void ClientDisconnected(uint64_t steamId);

    // Re-sends the connect handshake to clients that have not reported their socache
    // yet. Call once per game server frame; `now` is a parameter so tests can drive
    // the retry schedule without sleeping.
    void Update(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    // The Steam callbacks funnel into these, which tests drive directly.
    void HandleIncomingMessage(uint64_t steamId, const void *data, uint32_t size);
    void HandleSessionRequest(uint64_t steamId);
    void HandleSessionFailed(uint64_t steamId);

    void SendMessage(uint64_t steamId, const void *data, uint32_t size);
    void SendHostEvent(const EventData &event);

private:
    struct ClientSession
    {
        // The auth ticket the client validates the handshake against. It has to be
        // kept around because the game may hand us the same player again without a
        // disconnect in between, and because the connect message is re-sent.
        std::vector<uint8_t> ticket;

        // Number of connect messages sent for this session, and when the next one is
        // due. The handshake stops once the client's socache arrives or the attempts
        // run out.
        uint32_t connectAttempts{};
        std::chrono::steady_clock::time_point nextConnect{};

        // Set when the client answers the handshake with its socache. The reply is
        // the only acknowledgement this protocol has.
        bool hasSOCache{};
    };

    void SendConnectMessage(uint64_t steamId, ClientSession &session,
        std::chrono::steady_clock::time_point now);

    ISteamNetworkingMessages *const m_networkingMessages;
    std::unordered_map<uint64_t, ClientSession> m_clients;

    STEAM_GAMESERVER_CALLBACK(NetworkingServer,
        OnSessionRequest,
        SteamNetworkingMessagesSessionRequest_t,
        m_sessionRequest);

    STEAM_GAMESERVER_CALLBACK(NetworkingServer,
        OnSessionFailed,
        SteamNetworkingMessagesSessionFailed_t,
        m_sessionFailed);
};
