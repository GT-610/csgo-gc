#pragma once

#include <steam/isteamnetworkingmessages.h>

constexpr int NetMessageSendFlags = k_nSteamNetworkingSend_Reliable;
constexpr int NetMessageChannel = 7;

// Handshake messages must survive a broken session: with this flag the SDK restarts
// the session instead of returning k_EResultNoConnection, which would otherwise
// require an explicit CloseSessionWithUser before anything could be sent again.
constexpr int NetMessageHandshakeSendFlags =
    NetMessageSendFlags | k_nSteamNetworkingSend_AutoRestartBrokenSession;

// NOTE: these are used as gc message types!
// if they overlap with the game's gc messages, we're doomed
enum ENetworkMsg : uint32_t
{
    // sent by the server to client when they connect, data is the auth ticket
    k_EMsgNetworkConnect = (1u << 31) - 1,
    // sent by the client to server to keep round_mvp musickitmvps injectable
    k_EMsgNetworkMusicKitMVPState = (1u << 31) - 2,
};
