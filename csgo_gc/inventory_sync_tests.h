#pragma once

// Included by gc_message_tests.cpp to reuse its fake Steam transport and GC barriers.
namespace SyncTests
{
constexpr uint64_t ClientId = 76561197960265729ull;
constexpr uint64_t ServerId = 90100000000000001ull;
constexpr uint8_t Ticket[]{ 9, 2, 7, 4 };

static std::vector<uint8_t> Bytes(const GCMessageWrite &message)
{
    const auto *bytes = static_cast<const uint8_t *>(message.Data());
    return { bytes, bytes + message.Size() };
}

static InventorySync::SyncId ConnectId(const std::vector<uint8_t> &payload)
{
    GCMessageRead message{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
    uint32_t size = message.ReadUint32();
    message.ReadData(size);
    InventorySync::SyncId id;
    if (InventorySync::ReadConnectExtension(message, id) != InventorySync::ExtensionResult::Supported) return {};
    return id;
}

static std::vector<uint8_t> Delta(uint64_t itemId = 1)
{
    CSOEconItem item;
    item.set_id(itemId);
    item.add_equipped_state()->set_new_class(2);
    CMsgSOSingleObject object;
    object.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    object.mutable_owner_soid()->set_id(ClientId);
    object.set_type_id(SOTypeItem);
    object.set_version(17);
    object.set_object_data(item.SerializeAsString());
    return Bytes(GCMessageWrite{ k_ESOMsg_Update, object });
}

struct Fixture
{
    TestSteamNetworkingMessages clientWire;
    TestSteamNetworkingMessages serverWire;
    NetworkingClient client{ &clientWire };
    NetworkingServer server{ &serverWire };
    ClientGC clientGC{ ClientId };
    ServerGC serverGC;
    EventData route;
    std::vector<std::vector<uint8_t>> delivered;

    Fixture()
    {
        client.SetAuthTicket(11, Ticket, sizeof(Ticket));
        server.ClientConnected(ClientId, Ticket, sizeof(Ticket));
    }

    void Connect(const std::vector<uint8_t> &payload)
    {
        client.HandleIncomingMessage(&clientGC, ServerId, payload.data(), static_cast<uint32_t>(payload.size()));
    }

    void DrainClient()
    {
        clientGC.RunRconCommand("ping");
        std::vector<EventData> events;
        clientGC.GetHostEvents(events);
        for (const auto &event : events)
            if (event.type == static_cast<int>(HostEvent::NetMessage))
            {
                route = event;
                client.SendHostEvent(event);
            }
    }

    bool Deliver(const std::vector<uint8_t> &payload)
    {
        if (!server.HandleIncomingMessage(ClientId, payload.data(), static_cast<uint32_t>(payload.size()))) return false;
        bool accepted = false;
        for (auto &event : ValidateServerPayload(serverGC, ClientId, server.Generation(ClientId),
            payload.data(), static_cast<uint32_t>(payload.size())))
        {
            if (event.type == static_cast<int>(HostEvent::ValidatedNetMessage))
                accepted |= server.DeliverValidatedEvent(event, [this](uint32_t, std::vector<uint8_t> &&message)
                    { delivered.push_back(std::move(message)); });
            if (event.type == static_cast<int>(HostEvent::InventorySyncRejected)) server.RejectSync(event);
        }
        return accepted;
    }

    void Status(const std::vector<uint8_t> &payload)
    {
        client.HandleIncomingMessage(&clientGC, ServerId, payload.data(), static_cast<uint32_t>(payload.size()));
    }

    void Queue(const std::vector<uint8_t> &payload)
    {
        auto event = route;
        event.buffer = payload;
        client.SendHostEvent(event);
    }
};

static bool CodecRejectsMalformedFrames()
{
    InventorySync::Batch batch;
    batch.id = { 123, 1 };
    batch.full = true;
    batch.revision = 1;
    batch.messages.push_back(BuildServerSocacheMessage(ClientId));
    auto packet = Bytes(InventorySync::BuildBatch(batch));
    auto validBatch = [](const std::vector<uint8_t> &payload)
    {
        GCMessageRead reader{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
        InventorySync::Batch parsed;
        return InventorySync::ReadBatch(reader, parsed);
    };
    if (!validBatch(packet)) return false;
    for (size_t size = 0; size < packet.size(); ++size)
    {
        std::vector<uint8_t> truncated(packet.begin(), packet.begin() + size);
        if (validBatch(truncated)) return false;
    }
    auto tail = packet;
    tail.push_back(0);
    if (validBatch(tail)) return false;
    batch.id.instance = 0;
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.id = { 123, 1 };
    batch.full = false;
    batch.baseRevision = UINT64_MAX;
    batch.revision = 0;
    batch.messages = { Delta() };
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.baseRevision = 1;
    batch.revision = 3;
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.revision = 2;
    batch.messages = { Delta(1), Delta(2) };
    if (!validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.messages = { BuildServerSocacheMessage(ClientId) };
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.messages = { Bytes(GCMessageWrite{ k_EMsgGCClientHello, CMsgClientHello{} }) };
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.messages.assign(InventorySync::MaxMessages + 1, Delta());
    if (validBatch(Bytes(InventorySync::BuildBatch(batch)))) return false;
    std::vector<uint8_t> oversized(InventorySync::MaxPacketBytes + 1);
    std::copy(packet.begin(), packet.end(), oversized.begin());
    if (validBatch(oversized)) return false;
    auto status = Bytes(InventorySync::BuildStatus({ { 5, 6 }, 2, true }));
    for (size_t size = 0; size < status.size(); ++size)
    {
        GCMessageRead reader{ 0, status.data(), static_cast<uint32_t>(size) };
        InventorySync::Status parsed;
        if (InventorySync::ReadStatus(reader, parsed)) return false;
    }
    auto invalid = InventorySync::BuildStatus({ { 5, 6 }, 0, true });
    GCMessageRead reader{ 0, invalid.Data(), invalid.Size() };
    InventorySync::Status parsed;
    if (InventorySync::ReadStatus(reader, parsed)) return false;
    auto request = InventorySync::BuildResyncRequest({ 5, 6 });
    GCMessageRead requestReader{ 0, request.Data(), request.Size() };
    InventorySync::SyncId id;
    return InventorySync::ReadResyncRequest(requestReader, id) && id == InventorySync::SyncId{ 5, 6 };
}

static bool ConnectExtensionPreservesLegacyPrefix()
{
    Fixture fixture;
    const auto &payload = fixture.serverWire.sentPayloads.front();
    GCMessageRead oldReader{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
    uint32_t ticketSize = oldReader.ReadUint32();
    const void *ticket = oldReader.ReadData(ticketSize);
    if (!oldReader.IsValid() || ticketSize != sizeof(Ticket) || memcmp(ticket, Ticket, ticketSize)) return false;
    InventorySync::SyncId id;
    if (InventorySync::ReadConnectExtension(oldReader, id) != InventorySync::ExtensionResult::Supported) return false;
    const size_t extensionStart = 18 + 4 + sizeof(Ticket);
    for (size_t size = extensionStart + 1; size < payload.size(); ++size)
    {
        GCMessageRead truncated{ 0, payload.data(), static_cast<uint32_t>(size) };
        truncated.ReadData(4 + sizeof(Ticket));
        if (InventorySync::ReadConnectExtension(truncated, id) != InventorySync::ExtensionResult::Malformed) return false;
    }
    auto unknown = payload;
    uint32_t version = 99;
    memcpy(unknown.data() + extensionStart + 8, &version, sizeof(version));
    GCMessageRead future{ 0, unknown.data(), static_cast<uint32_t>(unknown.size()) };
    future.ReadData(4 + sizeof(Ticket));
    return InventorySync::ReadConnectExtension(future, id) == InventorySync::ExtensionResult::Unsupported;
}

static bool InvalidSnapshotsDoNotStopHandshake()
{
    Fixture fixture;
    const auto connected = std::chrono::steady_clock::now();
    auto invalid = BuildServerSocacheMessage(ClientId + 1);
    if (fixture.Deliver(invalid) || !fixture.delivered.empty()) return false;
    fixture.server.Update(connected + std::chrono::seconds{ 3 });
    if (fixture.serverWire.sentPayloads.size() != 2) return false;
    auto broken = BuildServerSocacheMessage(ClientId);
    broken.push_back(0xff);
    if (fixture.Deliver(broken) || !fixture.delivered.empty()) return false;
    fixture.server.Update(connected + std::chrono::seconds{ 8 });
    if (fixture.serverWire.sentPayloads.size() != 3) return false;
    return fixture.Deliver(BuildServerSocacheMessage(ClientId)) && fixture.delivered.size() == 1;
}

static bool VersionedSyncPreservesOrderingAndDeduplicates()
{
    Fixture fixture;
    fixture.Connect(fixture.serverWire.sentPayloads.front());
    fixture.DrainClient();
    if (fixture.clientWire.sentPayloads.size() != 1) return false;
    auto full = fixture.clientWire.sentPayloads.front();
    auto first = Delta(1);
    auto second = Delta(2);
    fixture.Queue(first);
    fixture.Queue(second);
    if (fixture.clientWire.sentPayloads.size() != 1 || !fixture.Deliver(full)) return false;
    auto fullAck = fixture.serverWire.sentPayloads.back();
    // Drop the first ACK and force a retry. It must be byte-for-byte identical.
    fixture.client.Update(&fixture.clientGC, std::chrono::steady_clock::now() + std::chrono::seconds{ 3 });
    if (fixture.clientWire.sentPayloads.back() != full) return false;
    if (fixture.Deliver(full) || fixture.delivered.size() != 1) return false;
    fixture.Status(fixture.serverWire.sentPayloads.back());
    if (fixture.clientWire.sentPayloads.size() != 3 || !fixture.Deliver(fixture.clientWire.sentPayloads.back())) return false;
    auto deltaAck = fixture.serverWire.sentPayloads.back();
    fixture.Status(deltaAck);
    if (!fixture.Deliver(fixture.clientWire.sentPayloads.back())) return false;
    fixture.Status(fixture.serverWire.sentPayloads.back());
    if (fixture.delivered.size() != 3 || fixture.delivered[1] != first || fixture.delivered[2] != second) return false;
    // A late status cannot roll back the acknowledged baseline or start a resync.
    const auto sends = fixture.clientWire.sentPayloads.size();
    fixture.Status(fullAck);
    fixture.Status(deltaAck);
    fixture.client.Update(&fixture.clientGC);
    if (fixture.clientWire.sentPayloads.size() != sends) return false;
    fixture.server.Update(std::chrono::steady_clock::now() + std::chrono::seconds{ 61 });
    InventorySync::Status status;
    const auto &payload = fixture.serverWire.sentPayloads.back();
    GCMessageRead reader{ 0, payload.data(), static_cast<uint32_t>(payload.size()) };
    return InventorySync::ReadStatus(reader, status) && status.ready && status.revision == 3;
}

static bool MixedVersionsKeepLegacyWireFormat()
{
    // New server, old client: raw snapshots and SO updates remain raw to the game.
    Fixture fixture;
    auto full = BuildServerSocacheMessage(ClientId);
    if (!fixture.Deliver(full) || !fixture.Deliver(Delta()) || fixture.delivered.size() != 2) return false;
    if (fixture.serverWire.sentPayloads.size() != 1) return false; // no private ACK to old client
    fixture.server.Update(std::chrono::steady_clock::now() + std::chrono::seconds{ 120 });
    if (fixture.serverWire.sentPayloads.size() != 1) return false;
    // New client, old server: no extension means no private envelope.
    GCMessageWrite connect{ k_EMsgNetworkConnect };
    connect.WriteUint32(sizeof(Ticket));
    connect.WriteData(Ticket, sizeof(Ticket));
    fixture.Connect(Bytes(connect));
    fixture.DrainClient();
    if (fixture.clientWire.sentPayloads.empty()) return false;
    const auto &raw = fixture.clientWire.sentPayloads.back();
    GCMessageRead reader{ 0, raw.data(), static_cast<uint32_t>(raw.size()) };
    return reader.IsValid() && reader.IsProtobuf() && reader.TypeUnmasked() == k_ESOMsg_CacheSubscribed;
}

static bool SessionGenerationsRejectStaleWork()
{
    Fixture fixture;
    auto oldConnect = fixture.serverWire.sentPayloads.front();
    fixture.Connect(oldConnect);
    fixture.DrainClient();
    auto oldRoute = fixture.route;
    auto full = fixture.clientWire.sentPayloads.front();
    auto validated = ValidateServerPayload(fixture.serverGC, ClientId, fixture.server.Generation(ClientId),
        full.data(), static_cast<uint32_t>(full.size()));
    fixture.server.ClientDisconnected(ClientId);
    fixture.server.ClientConnected(ClientId, Ticket, sizeof(Ticket));
    auto newConnect = fixture.serverWire.sentPayloads.back();
    if (ConnectId(oldConnect) == ConnectId(newConnect)) return false;
    for (const auto &event : validated)
        if (event.type == static_cast<int>(HostEvent::ValidatedNetMessage)
            && fixture.server.DeliverValidatedEvent(event, [](uint32_t, std::vector<uint8_t> &&) {})) return false;
    fixture.Connect(newConnect);
    fixture.DrainClient();
    const auto sends = fixture.clientWire.sentPayloads.size();
    fixture.client.SendHostEvent(oldRoute);
    fixture.Connect(oldConnect);
    fixture.DrainClient();
    if (fixture.clientWire.sentPayloads.size() != sends) return false;
    if (fixture.Deliver(full)) return false;
    return fixture.Deliver(fixture.clientWire.sentPayloads.back());
}

static bool ResyncRebuildsSnapshotAndBoundsPendingWork()
{
    Fixture fixture;
    fixture.Connect(fixture.serverWire.sentPayloads.front());
    fixture.DrainClient();
    auto oldId = ConnectId(fixture.serverWire.sentPayloads.front());
    if (!fixture.Deliver(fixture.clientWire.sentPayloads.front())) return false;
    fixture.Status(fixture.serverWire.sentPayloads.back());
    fixture.client.HandleSessionFailed(ServerId);
    fixture.client.Update(&fixture.clientGC);
    auto request = fixture.clientWire.sentPayloads.back();
    GCMessageRead reader{ 0, request.data(), static_cast<uint32_t>(request.size()) };
    InventorySync::SyncId id;
    if (!InventorySync::ReadResyncRequest(reader, id) || id != oldId) return false;
    fixture.server.HandleIncomingMessage(ClientId, request.data(), static_cast<uint32_t>(request.size()));
    auto newConnect = fixture.serverWire.sentPayloads.back();
    if (ConnectId(newConnect) == oldId) return false;
    fixture.Connect(newConnect);
    fixture.DrainClient();
    if (!fixture.Deliver(fixture.clientWire.sentPayloads.back())) return false;
    fixture.Status(fixture.serverWire.sentPayloads.back());
    // Hold an in-flight delta; saturating its FIFO asks for one new snapshot rather
    // than silently forgetting intermediate changes or sending an unbased delta.
    fixture.Queue(Delta());
    CMsgSOSingleObject large;
    large.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    large.mutable_owner_soid()->set_id(ClientId);
    large.set_type_id(SOTypeItem);
    large.set_object_data(std::string(200000, 'x'));
    auto payload = Bytes(GCMessageWrite{ k_ESOMsg_Update, large });
    for (int i = 0; i < 7; ++i) fixture.Queue(payload);
    fixture.client.Update(&fixture.clientGC);
    const auto &last = fixture.clientWire.sentPayloads.back();
    GCMessageRead lastReader{ 0, last.data(), static_cast<uint32_t>(last.size()) };
    return InventorySync::ReadResyncRequest(lastReader, id) && id == ConnectId(newConnect);
}

static bool AuthAndStatusesStayBoundToTheCurrentPeer()
{
    Fixture fixture;
    auto connect = fixture.serverWire.sentPayloads.front();
    auto invalid = connect;
    invalid[22] ^= 0xff; // first ticket byte, after the basic header and length
    fixture.Connect(invalid);
    fixture.DrainClient();
    if (!fixture.clientWire.sentPayloads.empty()) return false;
    fixture.Connect(connect);
    fixture.DrainClient();
    auto oldId = ConnectId(connect);
    auto firstFull = fixture.clientWire.sentPayloads.back();
    if (!fixture.Deliver(firstFull)) return false;
    auto ack = fixture.serverWire.sentPayloads.back();
    // A different peer cannot acknowledge this client's in-flight snapshot.
    fixture.Queue(Delta());
    const auto before = fixture.clientWire.sentPayloads.size();
    fixture.client.HandleIncomingMessage(&fixture.clientGC, ServerId + 1,
        ack.data(), static_cast<uint32_t>(ack.size()));
    if (fixture.clientWire.sentPayloads.size() != before) return false;
    fixture.server.ResetSOCache(ClientId);
    fixture.server.Update();
    const auto newId = ConnectId(fixture.serverWire.sentPayloads.back());
    fixture.Connect(fixture.serverWire.sentPayloads.back());
    fixture.DrainClient();
    fixture.Queue(Delta(2));
    const auto newSends = fixture.clientWire.sentPayloads.size();
    fixture.Status(ack);
    fixture.client.Update(&fixture.clientGC);
    if (fixture.clientWire.sentPayloads.size() != newSends) return false;
    if (!fixture.Deliver(fixture.clientWire.sentPayloads.back())) return false;
    // After negotiating the new protocol, raw inventory traffic is no longer an ACK.
    return !fixture.Deliver(BuildServerSocacheMessage(ClientId))
        && oldId != newId;
}

static bool NativeRefreshTargetsOnlyItsOwner()
{
    Fixture fixture;
    const uint64_t other = ClientId + 1;
    fixture.server.ClientConnected(other, Ticket, sizeof(Ticket));
    auto original = fixture.server.Generation(ClientId);
    auto otherGeneration = fixture.server.Generation(other);
    CMsgSOCacheSubscriptionRefresh refresh;
    refresh.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    refresh.mutable_owner_soid()->set_id(ClientId);
    GCMessageWrite message{ k_ESOMsg_CacheSubscriptionRefresh, refresh };
    fixture.serverGC.PostToGC(GCEvent::Message, message.TypeMasked(), message.Data(), message.Size());
    bool sawRefresh = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{ 1 };
    while (!sawRefresh && std::chrono::steady_clock::now() < deadline)
    {
        std::vector<EventData> events;
        fixture.serverGC.GetHostEvents(events);
        for (const auto &event : events)
            if (event.type == static_cast<int>(HostEvent::ResetSOCaches) && event.id == ClientId)
            {
                sawRefresh = true;
                fixture.server.ResetSOCache(event.id);
            }
        std::this_thread::yield();
    }
    if (!sawRefresh || fixture.server.Generation(ClientId) == original
        || fixture.server.Generation(other) != otherGeneration) return false;
    original = fixture.server.Generation(ClientId);
    fixture.server.ResetSOCache(); // same path used by server_spawn
    return fixture.server.Generation(ClientId) != original && fixture.server.Generation(other) != otherGeneration;
}

static bool VersionedSnapshotsRejectSanitizationAndPartialBatches()
{
    Fixture fixture;
    auto id = ConnectId(fixture.serverWire.sentPayloads.front());
    CMsgSOCacheSubscribed snapshot;
    snapshot.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    snapshot.mutable_owner_soid()->set_id(ClientId);
    CSOEconItem hidden;
    hidden.set_id(1); // unequipped: the legacy validator would sanitize it
    auto *object = snapshot.add_objects();
    object->set_type_id(SOTypeItem);
    object->add_object_data(hidden.SerializeAsString());
    InventorySync::Batch batch;
    batch.id = id;
    batch.full = true;
    batch.revision = 1;
    batch.messages = { Bytes(GCMessageWrite{ k_ESOMsg_CacheSubscribed, snapshot }) };
    if (fixture.Deliver(Bytes(InventorySync::BuildBatch(batch))) || !fixture.delivered.empty()) return false;
    // The rejection changed the generation, and old versioned work stays invalid.
    if (fixture.server.Generation(ClientId) == id.generation) return false;
    fixture.server.Update(std::chrono::steady_clock::now() + std::chrono::seconds{ 61 });
    id = ConnectId(fixture.serverWire.sentPayloads.back());
    batch.id = id;
    batch.messages = { BuildServerSocacheMessage(ClientId) };
    if (!fixture.Deliver(Bytes(InventorySync::BuildBatch(batch)))) return false;
    batch.full = false;
    batch.baseRevision = 1;
    batch.revision = 2;
    auto bad = Delta(2);
    GCMessageRead badReader{ 0, bad.data(), static_cast<uint32_t>(bad.size()) };
    CMsgSOSingleObject badOwner;
    if (!badReader.ReadProtobuf(badOwner)) return false;
    badOwner.mutable_owner_soid()->set_id(ClientId + 1);
    batch.messages = { Delta(1), Bytes(GCMessageWrite{ k_ESOMsg_Update, badOwner }) };
    if (fixture.Deliver(Bytes(InventorySync::BuildBatch(batch)))) return false;
    return fixture.delivered.size() == 1;
}
}
