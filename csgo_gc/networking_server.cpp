#include "stdafx.h"
#include "networking_server.h"
#include "gc_shared.h"

constexpr uint32_t MaxConnectAttempts = 6;
constexpr std::chrono::seconds ConnectRetryIdleDelay{ 60 };

static std::chrono::seconds ConnectRetryDelay(uint32_t attempts)
{
    std::chrono::seconds delay{ 2 };
    for (uint32_t i = 1; i < attempts && delay < std::chrono::seconds{ 30 }; ++i)
        delay *= 2;
    return std::min(delay, std::chrono::seconds{ 30 });
}

static uint64_t NewInstanceId()
{
    std::random_device random;
    uint64_t value = (uint64_t{ random() } << 32) | random();
    return value ? value : 1;
}

NetworkingServer::NetworkingServer(ISteamNetworkingMessages *networkingMessages)
    : m_networkingMessages{ networkingMessages }
    , m_instance{ NewInstanceId() }
    , m_sessionRequest{ this, &NetworkingServer::OnSessionRequest }
    , m_sessionFailed{ this, &NetworkingServer::OnSessionFailed }
{
}

uint64_t NetworkingServer::Generation(uint64_t steamId) const
{
    auto it = m_clients.find(steamId);
    return it == m_clients.end() ? 0 : it->second.syncId.generation;
}

bool NetworkingServer::ReceiveMessage(SteamNetworkingMessage_t *&message)
{
    // A dropped/internal message must not stop polling other peers on the channel.
    while (m_networkingMessages->ReceiveMessagesOnChannel(NetMessageChannel, &message, 1))
    {
        if (message->GetSize() >= 0 && HandleIncomingMessage(message->m_identityPeer.GetSteamID64(),
            message->GetData(), static_cast<uint32_t>(message->GetSize()))) return true;
        message->Release();
    }
    return false;
}

bool NetworkingServer::HandleIncomingMessage(uint64_t steamId, const void *data, uint32_t size)
{
    auto it = m_clients.find(steamId);
    if (it == m_clients.end() || size > InventorySync::MaxPacketBytes) return false;
    auto &session = it->second;
    GCMessageRead message{ 0, data, size };
    if (!message.IsValid()) return false;
    if (!message.IsProtobuf() && message.TypeUnmasked() == k_EMsgNetworkInventoryResync)
    {
        InventorySync::SyncId id;
        const bool valid = InventorySync::ReadResyncRequest(message, id);
        const auto now = std::chrono::steady_clock::now();
        if (valid && id == session.syncId && now >= session.nextResync)
        {
            Restart(session);
            session.nextResync = now + std::chrono::seconds{ 2 };
            SendConnectMessage(steamId, session, now);
        }
        else if (valid && id.instance == session.syncId.instance && id.generation <= session.syncId.generation
            && now >= session.nextConnect)
        {
            // A repeat request for the preceding generation reuses this handshake.
            SendConnectMessage(steamId, session, now);
        }
        return false;
    }
    if (!message.IsProtobuf() && message.TypeUnmasked() == k_EMsgNetworkInventoryBatch)
    {
        InventorySync::Batch batch;
        if (!InventorySync::ReadBatch(message, batch) || batch.id != session.syncId
            || session.protocol == Protocol::Legacy) return false;
        if (session.hasSOCache && batch.revision <= session.revision)
        {
            SendStatus(steamId, session, std::chrono::steady_clock::now());
            return false;
        }
        if ((!session.hasSOCache && !batch.full)
            || (session.hasSOCache && batch.baseRevision != session.revision))
        {
            Restart(session);
            SendConnectMessage(steamId, session, std::chrono::steady_clock::now());
            return false;
        }
        return true; // bodies and owner are checked by ServerGC before commit
    }
    if (message.IsProtobuf() && InventorySync::IsSOCacheMessage(message.TypeUnmasked()))
    {
        if (session.protocol == Protocol::Versioned) return false;
        if (message.TypeUnmasked() == k_ESOMsg_CacheSubscribed)
            session.legacySnapshotReceived = true;
        // The worker and host preserve receive order. Early deltas can be
        // validated now, but only a committed snapshot lets them reach the game.
        if (!session.hasSOCache && !session.legacySnapshotReceived) return false;
    }
    return true;
}

static EResult SendMessageToUser(ISteamNetworkingMessages *networkingMessages, uint64_t steamId,
    const void *data, uint32_t size, int flags)
{
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    EResult result = networkingMessages->SendMessageToUser(identity, data, size, flags, NetMessageChannel);
    if (result != k_EResultOK)
        Platform::Print("SendMessageToUser failed for %llu: %d\n", steamId, static_cast<int>(result));
    return result;
}

void NetworkingServer::Restart(ClientSession &session)
{
    session.syncId = { m_instance, ++m_generation };
    session.hasSOCache = false;
    session.legacySnapshotReceived = false;
    session.revision = 0;
    session.connectAttempts = 0;
    session.nextConnect = std::chrono::steady_clock::now();
    // Preserve a negotiated protocol until EndAuthSession; never silently downgrade.
}

void NetworkingServer::SendConnectMessage(uint64_t steamId, ClientSession &session,
    std::chrono::steady_clock::time_point now)
{
    GCMessageWrite message{ k_EMsgNetworkConnect };
    message.WriteUint32(static_cast<uint32_t>(session.ticket.size()));
    if (!session.ticket.empty())
        message.WriteData(session.ticket.data(), static_cast<uint32_t>(session.ticket.size()));
    InventorySync::WriteConnectExtension(message, session.syncId);
    EResult result = SendMessageToUser(m_networkingMessages, steamId,
        message.Data(), message.Size(), NetMessageHandshakeSendFlags);
    if (result == k_EResultNoConnection)
    {
        SteamNetworkingIdentity identity;
        identity.SetSteamID64(steamId);
        m_networkingMessages->CloseSessionWithUser(identity);
    }
    if (session.connectAttempts == 0)
        Platform::Print("NetworkingServer: requesting SOCache from %llu (generation %llu)\n",
            steamId, session.syncId.generation);
    else if (session.connectAttempts == MaxConnectAttempts - 1)
        Platform::Print("NetworkingServer: %llu exhausted fast probes; continuing every 60s\n", steamId);
    if (session.connectAttempts < MaxConnectAttempts) ++session.connectAttempts;
    session.nextConnect = now + (session.connectAttempts >= MaxConnectAttempts
        ? ConnectRetryIdleDelay : ConnectRetryDelay(session.connectAttempts));
}

void NetworkingServer::SendStatus(uint64_t steamId, ClientSession &session,
    std::chrono::steady_clock::time_point now)
{
    auto message = InventorySync::BuildStatus({ session.syncId, session.revision, session.hasSOCache });
    SendMessageToUser(m_networkingMessages, steamId, message.Data(), message.Size(), NetMessageHandshakeSendFlags);
    session.nextStatus = now + ConnectRetryIdleDelay;
}

void NetworkingServer::Update(std::chrono::steady_clock::time_point now)
{
    for (auto &[steamId, session] : m_clients)
    {
        if (!session.hasSOCache && now >= session.nextConnect) SendConnectMessage(steamId, session, now);
        else if (session.hasSOCache && session.protocol == Protocol::Versioned && now >= session.nextStatus)
            SendStatus(steamId, session, now);
    }
}

void NetworkingServer::ClientConnected(uint64_t steamId, const void *ticket, uint32_t ticketSize)
{
    auto [it, inserted] = m_clients.try_emplace(steamId);
    auto &session = it->second;
    // Standard Steam pairs successful BeginAuthSession/EndAuthSession. This branch
    // is defensive for direct callers, not an expected duplicate Steam auth flow.
    if (!inserted) Platform::Print("Refreshing GC session for %llu\n", steamId);
    session.ticket.clear();
    if (ticket && ticketSize)
    {
        const auto *bytes = static_cast<const uint8_t *>(ticket);
        session.ticket.assign(bytes, bytes + ticketSize);
    }
    session.protocol = Protocol::Unknown;
    Restart(session);
    SendConnectMessage(steamId, session, std::chrono::steady_clock::now());
}

void NetworkingServer::ClientDisconnected(uint64_t steamId)
{
    if (!m_clients.erase(steamId)) return;
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    m_networkingMessages->CloseChannelWithUser(identity, NetMessageChannel);
}

void NetworkingServer::ResetSOCache(uint64_t steamId)
{
    for (auto &[peer, session] : m_clients)
        if (!steamId || peer == steamId) Restart(session);
}

void NetworkingServer::RejectSync(const EventData &event)
{
    auto it = m_clients.find(event.id);
    if (it != m_clients.end() && event.generation == it->second.syncId.generation)
    {
        // Do not spin on a peer whose projected cache is persistently invalid.
        Restart(it->second);
        it->second.nextConnect += ConnectRetryIdleDelay;
        it->second.nextResync = it->second.nextConnect;
    }
}

bool NetworkingServer::DeliverValidatedEvent(const EventData &event,
    const std::function<void(uint32_t, std::vector<uint8_t> &&)> &deliver)
{
    auto it = m_clients.find(event.id);
    if (it == m_clients.end() || !event.generation || event.generation != it->second.syncId.generation)
        return false;
    auto &session = it->second;
    GCMessageRead message{ 0, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()) };
    if (!message.IsValid()) return false;
    if (!message.IsProtobuf() && message.TypeUnmasked() == k_EMsgNetworkInventoryBatch)
    {
        InventorySync::Batch batch;
        if (!InventorySync::ReadBatch(message, batch) || batch.id != session.syncId
            || session.protocol == Protocol::Legacy) return false;
        if (session.hasSOCache && batch.revision <= session.revision)
        {
            SendStatus(event.id, session, std::chrono::steady_clock::now());
            return false;
        }
        if (batch.full ? session.hasSOCache : (!session.hasSOCache || batch.baseRevision != session.revision))
            return false;
        // Called on the host thread: no callbacks consume half a batch before this
        // loop finishes. ACK means queued, not that server.dll applied the objects.
        for (auto &payload : batch.messages)
        {
            GCMessageRead inner{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
            deliver(inner.TypeMasked(), std::move(payload));
        }
        session.protocol = Protocol::Versioned;
        session.hasSOCache = true;
        session.revision = batch.revision;
        if (batch.full)
            Platform::Print("NetworkingServer: queued versioned SOCache from %llu (generation %llu, revision %llu)\n",
                event.id, session.syncId.generation, session.revision);
        SendStatus(event.id, session, std::chrono::steady_clock::now());
        return true;
    }
    if (!message.IsProtobuf() && message.TypeUnmasked() != k_EMsgNetworkMusicKitMVPState) return false;
    if (message.IsProtobuf() && InventorySync::IsSOCacheMessage(message.TypeUnmasked()))
    {
        if (session.protocol == Protocol::Versioned) return false;
        if (message.TypeUnmasked() == k_ESOMsg_CacheSubscribed)
        {
            session.protocol = Protocol::Legacy;
            session.hasSOCache = true;
        }
        else if (!session.hasSOCache) return false;
    }
    auto payload = event.buffer;
    deliver(message.TypeMasked(), std::move(payload));
    return true;
}

void NetworkingServer::SendMessage(uint64_t steamId, const void *data, uint32_t size)
{
    if (!m_clients.contains(steamId)) return;
    if (SendMessageToUser(m_networkingMessages, steamId, data, size, NetMessageSendFlags)
        != k_EResultNoConnection) return;
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    m_networkingMessages->CloseSessionWithUser(identity);
    // An ordinary send discovering a broken path also invalidates its cache base.
    HandleSessionFailed(steamId);
    SendMessageToUser(m_networkingMessages, steamId, data, size, NetMessageSendFlags);
}

void NetworkingServer::SendHostEvent(const EventData &event)
{
    assert(event.type == static_cast<int>(HostEvent::NetMessage));
    if (event.generation && event.generation != Generation(event.id)) return;
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
    if (it == m_clients.end()) return;
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(steamId);
    m_networkingMessages->AcceptSessionWithUser(identity);
    if (!it->second.hasSOCache) SendConnectMessage(steamId, it->second, std::chrono::steady_clock::now());
}

void NetworkingServer::HandleSessionFailed(uint64_t steamId)
{
    auto it = m_clients.find(steamId);
    if (it != m_clients.end()) Restart(it->second);
}
