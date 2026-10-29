#include "stdafx.h"
#include "networking_server.h"
#include "gc_message.h"
#include "gc_shared.h"

// The handshake is sent once when the client connects, which can happen before the
// P2P session exists, and is retried on a doubling delay until the client answers
// with its socache. The delays add up to about a minute, which covers map loads.
constexpr uint32_t MaxConnectAttempts = 6;
constexpr std::chrono::seconds ConnectRetryBaseDelay{ 2 };
constexpr std::chrono::seconds ConnectRetryMaxDelay{ 30 };

static std::chrono::seconds ConnectRetryDelay(uint32_t attempts)
{
    // attempts is the number of connect messages sent so far
    std::chrono::seconds delay = ConnectRetryBaseDelay;
    for (uint32_t i = 1; i < attempts && delay < ConnectRetryMaxDelay; i++)
    {
        delay *= 2;
    }

    return std::min(delay, ConnectRetryMaxDelay);
}

NetworkingServer::NetworkingServer(ISteamNetworkingMessages *networkingMessages)
    : m_networkingMessages{ networkingMessages }
    , m_sessionRequest{ this, &NetworkingServer::OnSessionRequest }
    , m_sessionFailed{ this, &NetworkingServer::OnSessionFailed }
{
}

bool NetworkingServer::ReceiveMessage(SteamNetworkingMessage_t *&message)
{
    if (!m_networkingMessages->ReceiveMessagesOnChannel(NetMessageChannel, &message, 1))
    {
        return false;
    }

    uint64_t steamId = message->m_identityPeer.GetSteamID64();

    // see if we have a session
    if (!m_clients.contains(steamId))
    {
        Platform::Print("NetworkingServer: ignored message from %llu (no session)\n", steamId);
        message->Release();
        return false;
    }

    HandleIncomingMessage(steamId, message->GetData(), static_cast<uint32_t>(message->GetSize()));

    return true;
}

void NetworkingServer::HandleIncomingMessage(uint64_t steamId, const void *data, uint32_t size)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end() || it->second.hasSOCache)
    {
        return;
    }

    // The client answers the connect handshake with its socache, which is the only
    // acknowledgement this protocol has. A later socache is just an inventory update.
    GCMessageRead messageRead{ 0, data, size };
    if (messageRead.IsValid() && messageRead.IsProtobuf()
        && messageRead.TypeUnmasked() == k_ESOMsg_CacheSubscribed)
    {
        it->second.hasSOCache = true;
        Platform::Print("NetworkingServer: %llu sent their socache, handshake done after %u attempt(s)\n",
            steamId, it->second.connectAttempts);
    }
}

// helper for SteamNetworkingMessages::SendMessageToUser, returns the SDK's send result.
// What a broken session means differs per caller, so tearing it down is left to them.
static EResult SendMessageToUser(ISteamNetworkingMessages *networkingMessages, uint64_t steamId,
    const void *data, uint32_t size, int flags)
{
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);

    EResult result = networkingMessages->SendMessageToUser(
        identity,
        data,
        size,
        flags,
        NetMessageChannel);

    if (result != k_EResultOK)
    {
        Platform::Print("SendMessageToUser failed for %llu: %d\n", steamId, static_cast<int>(result));
    }

    return result;
}

void NetworkingServer::SendConnectMessage(uint64_t steamId, ClientSession &session,
    std::chrono::steady_clock::time_point now)
{
    // send a message, if the client has csgo_gc installed they will
    // reply with their so cache and we'll add them to our list
    GCMessageWrite messageWrite{ k_EMsgNetworkConnect };
    messageWrite.WriteUint32(static_cast<uint32_t>(session.ticket.size()));
    if (!session.ticket.empty())
    {
        messageWrite.WriteData(session.ticket.data(), static_cast<uint32_t>(session.ticket.size()));
    }

    const EResult result = SendMessageToUser(m_networkingMessages, steamId,
        messageWrite.Data(), messageWrite.Size(), NetMessageHandshakeSendFlags);

    if (result == k_EResultNoConnection)
    {
        // Only reachable when the auto-restart flag did not do its job. Acknowledge the
        // broken session so a later send can start a new one.
        SteamNetworkingIdentity identity;
        identity.SetSteamID64(steamId);
        m_networkingMessages->CloseSessionWithUser(identity);
    }

    session.connectAttempts++;
    session.nextConnect = now + ConnectRetryDelay(session.connectAttempts);

    if (session.connectAttempts == MaxConnectAttempts)
    {
        Platform::Print("NetworkingServer: giving up the connect handshake with %llu after %u attempts\n",
            steamId, session.connectAttempts);
    }
    else if (session.connectAttempts > 1)
    {
        Platform::Print("NetworkingServer: connect handshake with %llu, attempt %u/%u%s\n",
            steamId, session.connectAttempts, MaxConnectAttempts,
            result == k_EResultOK ? "" : " (send failed)");
    }
}

void NetworkingServer::Update(std::chrono::steady_clock::time_point now)
{
    for (auto &[steamId, session] : m_clients)
    {
        if (session.hasSOCache || session.connectAttempts >= MaxConnectAttempts
            || now < session.nextConnect)
        {
            continue;
        }

        SendConnectMessage(steamId, session, now);
    }
}

void NetworkingServer::ClientConnected(uint64_t steamId, const void *ticket, uint32_t ticketSize)
{
    auto [it, inserted] = m_clients.try_emplace(steamId);
    ClientSession &session = it->second;

    if (ticketSize && ticket)
    {
        const auto *ticketBytes = static_cast<const uint8_t *>(ticket);
        session.ticket.assign(ticketBytes, ticketBytes + ticketSize);
    }
    else
    {
        session.ticket.clear();
    }

    if (!inserted)
    {
        // The game can begin a new auth session for a player without telling us that
        // the previous one ended. The connect message carries this ticket and the
        // client validates it, so re-send the handshake with the current ticket and a
        // fresh retry budget.
        Platform::Print("got ClientConnected for %llu but they're already on the list, refreshing the session\n", steamId);
        session.hasSOCache = false;
        session.connectAttempts = 0;
    }

    SendConnectMessage(steamId, session, std::chrono::steady_clock::now());
}

void NetworkingServer::ClientDisconnected(uint64_t steamId)
{
    if (m_clients.erase(steamId) == 0)
    {
        Platform::Print("got ClientDisconnected for %llu but they're not on the list! ignoring\n", steamId);
        return;
    }

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    m_networkingMessages->CloseChannelWithUser(identity, NetMessageChannel);
}

void NetworkingServer::SendMessage(uint64_t steamId, const void *data, uint32_t size)
{
    if (!m_clients.contains(steamId))
    {
        Platform::Print("No csgo_gc session with %llu, not sending message!!!\n", steamId);
        return;
    }

    // Ordinary GC traffic does not carry the handshake's auto-restart flag, so a broken
    // session surfaces as k_EResultNoConnection instead of silently restarting. Close the
    // session to acknowledge that, then retry once: the retry is what re-opens it, and
    // giving up here would drop the message until the handshake happens to run again.
    // Closing only the channel would not do: the SDK closes the session when the last
    // channel goes away, so another open channel to the peer would keep it in the broken
    // state and leave the retry failing too.
    if (SendMessageToUser(m_networkingMessages, steamId, data, size, NetMessageSendFlags)
        != k_EResultNoConnection)
    {
        return;
    }

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    m_networkingMessages->CloseSessionWithUser(identity);

    const EResult result = SendMessageToUser(m_networkingMessages, steamId, data, size,
        NetMessageSendFlags);
    if (result != k_EResultOK)
    {
        Platform::Print("NetworkingServer: retrying the send to %llu failed: %d\n",
            steamId, static_cast<int>(result));
    }
}

void NetworkingServer::SendHostEvent(const EventData &event)
{
    assert(event.type == static_cast<int>(HostEvent::NetMessage));
    SendMessage(event.id, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()));
}

void NetworkingServer::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t *param)
{
    HandleSessionRequest(param->m_identityRemote.GetSteamID64());
}

void NetworkingServer::OnSessionFailed(SteamNetworkingMessagesSessionFailed_t *param)
{
    Platform::Print("OnSessionFailed: %s\n", param->m_info.m_szEndDebug);
    HandleSessionFailed(param->m_info.m_identityRemote.GetSteamID64());
}

void NetworkingServer::HandleSessionRequest(uint64_t steamId)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end())
    {
        Platform::Print("%llu sent a session request, we don't have a csgo_gc session, ignoring...\n", steamId);
        return;
    }

    Platform::Print("%llu sent a session request, we were playing GC with them so accept\n", steamId);

    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    if (!m_networkingMessages->AcceptSessionWithUser(identity))
    {
        Platform::Print("AcceptSessionWithUser with %llu failed???\n", steamId);
    }

    if (it->second.hasSOCache || it->second.connectAttempts >= MaxConnectAttempts)
    {
        return;
    }

    // The client is reaching out right now, so the path is up. No reason to wait for
    // the next retry slot.
    SendConnectMessage(steamId, it->second, std::chrono::steady_clock::now());
}

void NetworkingServer::HandleSessionFailed(uint64_t steamId)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end())
    {
        return;
    }

    // Whatever the client told us may have been lost with the session, so the socache
    // has to be asked for again instead of trusting the previous handshake.
    if (it->second.hasSOCache)
    {
        // The handshake did succeed, so its attempt budget is spent history. Give the
        // re-handshake a fresh budget; otherwise a client that only answered on the
        // last allowed attempt could never be reached again. A session that was never
        // acknowledged keeps its count, so a broken path does not buy extra attempts.
        it->second.connectAttempts = 0;
    }

    it->second.hasSOCache = false;
    it->second.nextConnect = std::chrono::steady_clock::now();
}
