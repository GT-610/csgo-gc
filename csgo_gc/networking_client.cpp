#include "stdafx.h"
#include "networking_client.h"
#include "gc_client.h"

NetworkingClient::NetworkingClient(ISteamNetworkingMessages *networkingMessages)
    : m_networkingMessages{ networkingMessages }
    , m_sessionRequest{ this, &NetworkingClient::OnSessionRequest }
    , m_sessionFailed{ this, &NetworkingClient::OnSessionFailed }
{
}

void NetworkingClient::SetNetworkingMessages(ISteamNetworkingMessages *networkingMessages)
{
    assert(networkingMessages);
    m_networkingMessages = networkingMessages;
}

void NetworkingClient::Update(ClientGC *gc, std::chrono::steady_clock::time_point now)
{
    if (m_clearGCContext && gc)
    {
        gc->PostToGC(GCEvent::SOCacheRequest, 0, nullptr, 0, m_generation);
        m_clearGCContext = false;
    }
    if (m_tickets.empty() && !m_serverSteamId) return;
    SteamNetworkingMessage_t *message;
    while (m_networkingMessages->ReceiveMessagesOnChannel(NetMessageChannel, &message, 1))
    {
        if (message->GetSize() >= 0)
            HandleIncomingMessage(gc, message->m_identityPeer.GetSteamID64(), message->GetData(),
                static_cast<uint32_t>(message->GetSize()));
        message->Release();
    }
    Pump(now);
}

void NetworkingClient::HandleIncomingMessage(ClientGC *gc, uint64_t steamId, const void *data, uint32_t size)
{
    if (size > InventorySync::MaxPacketBytes) return;
    GCMessageRead message{ 0, data, size };
    if (!message.IsValid()) return;
    if (HandleMessage(gc, steamId, message)) return;
    if (gc && m_serverSteamId && steamId == m_serverSteamId)
        gc->PostToGC(GCEvent::NetMessage, steamId, data, size, m_generation);
}

bool NetworkingClient::HandleMessage(ClientGC *gc, uint64_t steamId, GCMessageRead &message)
{
    if (message.IsProtobuf()) return false;
    if (message.TypeUnmasked() == k_EMsgNetworkConnect)
    {
        uint32_t ticketSize = message.ReadUint32();
        const void *ticket = message.ReadData(ticketSize);
        if (!message.IsValid() || !ticketSize) return true;
        InventorySync::SyncId id;
        auto extension = InventorySync::ReadConnectExtension(message, id);
        if (extension == InventorySync::ExtensionResult::Malformed) return true;
        bool validTicket = false;
        for (auto &[handle, auth] : m_tickets)
        {
            if (auth.buffer.size() == ticketSize && !memcmp(auth.buffer.data(), ticket, ticketSize))
            {
                auth.steamId = steamId;
                validTicket = true;
                break;
            }
        }
        if (!validTicket) return true;
        if (extension != InventorySync::ExtensionResult::Supported) id = {};
        if (steamId == m_serverSteamId && m_syncId.IsValid())
        {
            // Once negotiated, neither a delayed legacy packet nor an old epoch may
            // replace this baseline. A repeated handshake is not a new snapshot.
            if (!id.IsValid() || m_retiredInstances.contains(id.instance)
                || (id.instance == m_syncId.instance && id.generation < m_syncId.generation)) return true;
            if (id == m_syncId)
            {
                Pump(std::chrono::steady_clock::now());
                return true;
            }
            if (id.instance != m_syncId.instance) m_retiredInstances.insert(m_syncId.instance);
        }
        else m_retiredInstances.clear();
        ClearPending();
        m_syncId = id;
        m_serverSteamId = steamId;
        ++m_generation;
        m_resync = false;
        m_clearGCContext = false;
        m_waitingSnapshot = id.IsValid();
        Platform::Print("NetworkingClient: %s SOCache handshake with %llu (generation %llu)\n",
            id.IsValid() ? "versioned" : "legacy", steamId, id.generation);
        if (gc) gc->PostToGC(GCEvent::SOCacheRequest, steamId, nullptr, 0, m_generation);
        return true;
    }
    if (message.TypeUnmasked() == k_EMsgNetworkInventoryStatus)
    {
        InventorySync::Status status;
        if (steamId != m_serverSteamId || !InventorySync::ReadStatus(message, status)
            || !m_syncId.IsValid() || status.id != m_syncId || m_resync) return true;
        if (!status.ready)
        {
            if (m_revision) RequestResync();
            return true;
        }
        if (status.revision <= m_revision) return true; // delayed status is not a rollback
        if (m_inFlight.empty() || status.revision != m_revision + 1)
        {
            RequestResync();
            return true;
        }
        m_revision = status.revision;
        m_inFlight.clear();
        m_sendAttempts = 0;
        m_nextSend = {};
        Pump(std::chrono::steady_clock::now());
        return true;
    }
    // Private frames never reach client.dll, including unknown/misdirected frames.
    return message.TypeUnmasked() == k_EMsgNetworkInventoryBatch
        || message.TypeUnmasked() == k_EMsgNetworkInventoryResync;
}

void NetworkingClient::ClearPending()
{
    m_pending = {};
    m_pendingBytes = 0;
    m_inFlight.clear();
    m_revision = 0;
    m_sendAttempts = 0;
    m_nextSend = {};
    m_waitingSnapshot = false;
}

void NetworkingClient::RequestResync()
{
    if (!m_syncId.IsValid() || m_resync) return;
    ClearPending();
    ++m_generation; // invalidate work already queued to or from ClientGC
    m_clearGCContext = true;
    m_resync = true;
    Platform::Print("NetworkingClient: requesting SOCache resync with %llu\n", m_serverSteamId);
}

void NetworkingClient::Pump(std::chrono::steady_clock::time_point now)
{
    if (!m_serverSteamId || !m_syncId.IsValid() || now < m_nextSend) return;
    if (m_resync)
    {
        auto request = InventorySync::BuildResyncRequest(m_syncId);
        SendRaw(request.Data(), request.Size());
    }
    else
    {
        if (m_inFlight.empty())
        {
            if (m_pending.empty()) return;
            auto payload = std::move(m_pending.front());
            m_pending.pop();
            m_pendingBytes -= payload.size();
            GCMessageRead inner{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
            InventorySync::Batch batch;
            batch.id = m_syncId;
            batch.baseRevision = m_revision;
            batch.revision = m_revision + 1;
            batch.full = inner.TypeUnmasked() == k_ESOMsg_CacheSubscribed;
            batch.messages.push_back(std::move(payload));
            auto message = InventorySync::BuildBatch(batch);
            const auto *bytes = static_cast<const uint8_t *>(message.Data());
            m_inFlight.assign(bytes, bytes + message.Size());
        }
        SendRaw(m_inFlight.data(), static_cast<uint32_t>(m_inFlight.size()));
    }
    ++m_sendAttempts;
    m_nextSend = now + (m_sendAttempts < 6 ? std::chrono::seconds{ 2 } : std::chrono::seconds{ 60 });
}

void NetworkingClient::SendHostEvent(const EventData &event)
{
    if (!m_serverSteamId || event.id != m_serverSteamId || event.generation != m_generation) return;
    GCMessageRead message{ 0, event.buffer.data(), static_cast<uint32_t>(event.buffer.size()) };
    if (!message.IsValid()) return;
    if (!m_syncId.IsValid() || !InventorySync::IsSOCacheMessage(message.TypeUnmasked()))
    {
        SendMessage(event.buffer.data(), static_cast<uint32_t>(event.buffer.size()));
        return;
    }
    if (m_resync) return;
    if (message.TypeUnmasked() == k_ESOMsg_CacheSubscribed)
    {
        if (!m_waitingSnapshot) { RequestResync(); return; }
        m_waitingSnapshot = false;
    }
    else if (m_waitingSnapshot) return;
    // Keep a single in-flight message and a bounded FIFO. Every emitted SO retains
    // its exact order; ACK loss replays only the envelope, not the game operation.
    if (event.buffer.size() > InventorySync::MaxPacketBytes - 128
        || m_pendingBytes + event.buffer.size() > InventorySync::MaxPendingBytes
        || m_revision == UINT64_MAX)
    {
        RequestResync();
        return;
    }
    m_pending.push(event.buffer);
    m_pendingBytes += event.buffer.size();
    Pump(std::chrono::steady_clock::now());
}

EResult NetworkingClient::SendRaw(const void *data, uint32_t size)
{
    if (!m_serverSteamId) return k_EResultNoConnection;
    SteamNetworkingIdentity identity;
    identity.SetSteamID64(m_serverSteamId);
    EResult result = m_networkingMessages->SendMessageToUser(identity, data, size,
        NetMessageHandshakeSendFlags, NetMessageChannel);
    if (result == k_EResultNoConnection) m_networkingMessages->CloseSessionWithUser(identity);
    if (result != k_EResultOK)
        Platform::Print("NetworkingClient: send to %llu failed: %d\n", m_serverSteamId, static_cast<int>(result));
    return result;
}

void NetworkingClient::SendMessage(const void *data, uint32_t size)
{
    SendRaw(data, size);
}

void NetworkingClient::SetAuthTicket(uint32_t handle, const void *data, uint32_t size)
{
    if (m_tickets.contains(handle)) ClearAuthTicket(handle);
    auto &ticket = m_tickets[handle];
    if (size)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        ticket.buffer.assign(bytes, bytes + size);
    }
}

void NetworkingClient::ClearAuthTicket(uint32_t handle)
{
    auto it = m_tickets.find(handle);
    if (it == m_tickets.end()) return;
    if (it->second.steamId)
    {
        SteamNetworkingIdentity identity;
        identity.SetSteamID64(it->second.steamId);
        m_networkingMessages->CloseChannelWithUser(identity, NetMessageChannel);
        if (it->second.steamId == m_serverSteamId)
        {
            m_serverSteamId = 0;
            m_syncId = {};
            m_retiredInstances.clear();
            ClearPending();
            ++m_generation;
            m_clearGCContext = true;
            m_resync = false;
        }
    }
    m_tickets.erase(it);
}

void NetworkingClient::HandleSessionFailed(uint64_t steamId)
{
    if (steamId == m_serverSteamId) RequestResync();
}

void NetworkingClient::OnSessionRequest(SteamNetworkingMessagesSessionRequest_t *param)
{
    if (param->m_identityRemote.GetSteamID().BGameServerAccount())
        m_networkingMessages->AcceptSessionWithUser(param->m_identityRemote);
}

void NetworkingClient::OnSessionFailed(SteamNetworkingMessagesSessionFailed_t *param)
{
    Platform::Print("NetworkingClient::OnSessionFailed: %s\n", param->m_info.m_szEndDebug);
    HandleSessionFailed(param->m_info.m_identityRemote.GetSteamID64());
}
