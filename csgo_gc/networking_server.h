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

    void SendMessage(uint64_t steamId, const void *data, uint32_t size);
    void SendHostEvent(const EventData &event);

private:
    struct ClientSession
    {
        // The auth ticket the client validates the handshake against. It has to be
        // kept around because the game may hand us the same player again without a
        // disconnect in between, and because the connect message may be re-sent.
        std::vector<uint8_t> ticket;
    };

    void SendConnectMessage(uint64_t steamId, const ClientSession &session);

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
