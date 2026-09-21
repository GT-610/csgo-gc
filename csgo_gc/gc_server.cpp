#include "stdafx.h"
#include "gc_server.h"
#include "config.h"
#include "game_types.h"
#include "gc_const.h"
#include "gc_const_csgo.h"
#include "graffiti.h"
#include "item_schema.h"
#include "music_kit.h"
#include "networking_shared.h"

// yuck!! needed for CSteamID (construct full id from account id)
#include "steam/steamclientpublic.h"

ServerGC::ServerGC()
{
    // also called from ClientGC's constructor
    Graffiti::Initialize();

    StartThread();

    Platform::Print("ServerGC spawned\n");
}

ServerGC::~ServerGC()
{
    StopThread();
    Platform::Print("ServerGC destroyed\n");
}

bool ServerGC::RoundMVPMusicKitCountForUserId(int userId, int &musickitmvps) const
{
    // Evaluate the gate at publish time. A server can change its game mode between
    // rounds, so a value cached at connect time would go stale.
    if (!MusicKit::ShouldTrackStatTrak(GetConfig().MusicKitStatTrakGate()))
    {
        return false;
    }

    std::lock_guard<std::mutex> lock{ m_musicKitMVPStateMutex };

    auto it = m_musicKitMVPStateByUserId.find(userId);
    if (it == m_musicKitMVPStateByUserId.end() || !it->second.hasEquippedStatTrakMusicKit)
    {
        return false;
    }

    musickitmvps = static_cast<int>(it->second.currentMVPs + 1);
    return true;
}

// Stores a player's music kit state, keeping both indexes consistent and removing
// the player entirely when no StatTrak music kit is equipped. Callers must hold the
// mutex.
void ServerGC::StoreMusicKitStateLocked(uint64_t steamId, int userId, uint64_t itemId,
    uint32_t currentMVPs, bool hasEquippedStatTrakMusicKit)
{
    if (!hasEquippedStatTrakMusicKit)
    {
        ForgetMusicKitStateLocked(steamId);
        return;
    }

    MusicKitMVPState state;
    state.userId = userId;
    state.itemId = itemId;
    state.currentMVPs = currentMVPs;
    state.hasEquippedStatTrakMusicKit = true;

    auto &slot = m_musicKitMVPStateBySteamId[steamId];

    // Drop the previous user id index entry when this player's user id changed, so
    // a new server session cannot leave a stale lookup behind.
    if (slot.userId > 0 && slot.userId != state.userId)
    {
        m_musicKitMVPStateByUserId.erase(slot.userId);
    }

    slot = state;

    if (state.userId > 0)
    {
        m_musicKitMVPStateByUserId[state.userId] = state;
    }
}

void ServerGC::ForgetMusicKitStateLocked(uint64_t steamId)
{
    auto stateIt = m_musicKitMVPStateBySteamId.find(steamId);
    if (stateIt == m_musicKitMVPStateBySteamId.end())
    {
        return;
    }

    if (stateIt->second.userId > 0)
    {
        m_musicKitMVPStateByUserId.erase(stateIt->second.userId);
    }

    m_musicKitMVPStateBySteamId.erase(stateIt);
}

void ServerGC::TrackMusicKitFromItem(uint64_t steamId, const CSOEconItem &item)
{
    if (!item.has_id())
    {
        return;
    }

    bool isEquippedMusicKit = MusicKit::IsEquippedMusicKit(item, ItemSchema::LoadoutSlotMusicKit);
    MusicKit::AttributeValues values = isEquippedMusicKit
        ? MusicKit::ReadAttributeValues(item)
        : MusicKit::AttributeValues{};

    std::lock_guard<std::mutex> lock{ m_musicKitMVPStateMutex };

    if (!isEquippedMusicKit || !MusicKit::IsStatTrak(values))
    {
        // The tracked kit is no longer an equipped StatTrak music kit: it was
        // unequipped, destroyed, or replaced. The old counter no longer applies.
        auto stateIt = m_musicKitMVPStateBySteamId.find(steamId);
        if (stateIt != m_musicKitMVPStateBySteamId.end() && stateIt->second.itemId == item.id())
        {
            ForgetMusicKitStateLocked(steamId);
        }
        return;
    }

    // Keep whatever user id we already know; it arrives on a separate message.
    auto stateIt = m_musicKitMVPStateBySteamId.find(steamId);
    int userId = stateIt != m_musicKitMVPStateBySteamId.end() ? stateIt->second.userId : 0;

    StoreMusicKitStateLocked(steamId, userId, item.id(), values.killEater, true);
}

void ServerGC::TrackMusicKitFromCache(uint64_t steamId, const CMsgSOCacheSubscribed &message)
{
    for (const CMsgSOCacheSubscribed_SubscribedType &object : message.objects())
    {
        if (object.type_id() != SOTypeItem)
        {
            continue;
        }

        for (const std::string &objectData : object.object_data())
        {
            CSOEconItem item;
            if (item.ParseFromString(objectData))
            {
                TrackMusicKitFromItem(steamId, item);
            }
        }
    }
}

void ServerGC::TrackMusicKitFromNetMessage(uint64_t steamId, const void *data, uint32_t size)
{
    // Parsed from the raw buffer because the validators above consume their own
    // GCMessageRead. A message that cannot be re-read simply carries no music kit
    // state; it has already been validated, so nothing else depends on this.
    GCMessageRead messageRead{ 0, data, size };
    if (!messageRead.IsValid() || !messageRead.IsProtobuf())
    {
        return;
    }

    switch (messageRead.TypeUnmasked())
    {
    case k_ESOMsg_CacheSubscribed:
    {
        CMsgSOCacheSubscribed message;
        if (messageRead.ReadProtobuf(message))
        {
            TrackMusicKitFromCache(steamId, message);
        }
        break;
    }

    case k_ESOMsg_Create:
    case k_ESOMsg_Update:
    {
        CMsgSOSingleObject message;
        if (messageRead.ReadProtobuf(message) && message.type_id() == SOTypeItem)
        {
            CSOEconItem item;
            if (item.ParseFromString(message.object_data()))
            {
                TrackMusicKitFromItem(steamId, item);
            }
        }
        break;
    }

    case k_ESOMsg_UpdateMultiple:
    {
        CMsgSOMultipleObjects message;
        if (messageRead.ReadProtobuf(message))
        {
            for (const CMsgSOMultipleObjects_SingleObject &object : message.objects_modified())
            {
                if (object.type_id() != SOTypeItem)
                {
                    continue;
                }

                CSOEconItem item;
                if (item.ParseFromString(object.object_data()))
                {
                    TrackMusicKitFromItem(steamId, item);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}

void ServerGC::HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer)
{
    switch (type)
    {
    case GCEvent::Message:
        HandleMessage(static_cast<uint32_t>(id), buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::NetMessage:
        HandleNetMessage(id, buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::ClientSOCacheUnsubscribe:
        HandleClientSOCacheUnsubscribe(id);
        break;

    default:
        Platform::Print("ServerGC::HandleEvent: unknown event type %d\n", static_cast<int>(type));
        break;
    }
}

void ServerGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        Platform::Print("ServerGC::HandleMessage: invalid message\n");
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCServerHello:
            SendServerWelcome();
            break;

        case k_EMsgGCCStrike15_v2_Server2GCClientValidate:
            // server doesn't want a response so ignore
            break;

        case k_EMsgGC_IncrementKillCountAttribute:
            IncrementKillCountAttribute(messageRead);
            break;

        default:
            Platform::Print("ServerGC::HandleMessage: unhandled protobuf message %s)\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ServerGC::HandleClientSOCacheUnsubscribe(uint64_t steamId)
{
    Platform::Print("HandleClientSOCacheUnsubscribe: %llu\n", steamId);

    {
        std::lock_guard<std::mutex> lock{ m_musicKitMVPStateMutex };
        ForgetMusicKitStateLocked(steamId);
    }

    CMsgSOCacheUnsubscribed message;
    message.mutable_owner_soid()->set_type(SoIdTypeSteamId);
    message.mutable_owner_soid()->set_id(steamId);

    GCMessageWrite write{ k_ESOMsg_CacheUnsubscribed, message };
    PostToHost(HostEvent::Message, write.TypeMasked(), write.Data(), write.Size());
}

template<typename T>
static bool ValidateMessageOwnerSOID(GCMessageRead &messageRead, uint64_t steamId, std::optional<GCMessageWrite> &)
{
    T message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: parsing failed\n", steamId);
        return false;
    }

    if (message.owner_soid().type() != SoIdTypeSteamId
        || message.owner_soid().id() != steamId)
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: steam id mismatch (message has %llu)\n",
            steamId, message.owner_soid().id());
        return false;
    }

    return true;
}

// FIXME: made up
constexpr int MaxServerSOCacheItems = 64;

static bool RemoveUnequippedItems(CMsgSOCacheSubscribed &message, int &itemCount)
{
    bool modified = false;

    for (auto it = message.mutable_objects()->begin(); it != message.mutable_objects()->end(); it++)
    {
        if (it->type_id() != SOTypeItem)
        {
            continue;
        }

        for (auto obj = it->mutable_object_data()->begin(); obj != it->mutable_object_data()->end(); )
        {
            CSOEconItem item;
            if (!item.ParseFromString(*obj) || !item.equipped_state_size())
            {
                obj = it->mutable_object_data()->erase(obj);
                modified = true;
            }
            else
            {
                obj++;
                itemCount++;
            }
        }
    }

    return modified;
}

template<>
bool ValidateMessageOwnerSOID<CMsgSOCacheSubscribed>(GCMessageRead &messageRead, uint64_t steamId, std::optional<GCMessageWrite> &sanitized)
{
    CMsgSOCacheSubscribed message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: parsing failed\n", steamId);
        return false;
    }

    if (message.owner_soid().type() != SoIdTypeSteamId
        || message.owner_soid().id() != steamId)
    {
        Platform::Print("ValidateMessageOwnerSOID %llu: steam id mismatch (message has %llu)\n",
            steamId, message.owner_soid().id());
        return false;
    }

    size_t oldSize = message.ByteSizeLong();

    int itemCount = 0;
    bool modified = RemoveUnequippedItems(message, itemCount);

    if (itemCount > MaxServerSOCacheItems)
    {
        Platform::Print("Client %llu socache has %d items (max allowed %d), ignoring\n", itemCount, MaxServerSOCacheItems);
        return false;
    }

    if (modified)
    {
        Platform::Print("SOCache from %llu had to be cleaned up (%zu -> %zu bytes)\n", steamId, oldSize, message.ByteSizeLong());
        sanitized.emplace(k_ESOMsg_CacheSubscribed, message);
    }

    return true;
}

void ServerGC::HandleNetMessage(uint64_t steamId, const void *data, uint32_t size)
{
    Platform::Print("HandleNetMessage: %llu, %u bytes\n", steamId, size);

    GCMessageRead validate{ 0, data, size };
    if (!validate.IsValid())
    {
        assert(false);
        return;
    }

    if (!validate.IsProtobuf())
    {
        if (validate.TypeUnmasked() == k_EMsgNetworkMusicKitMVPState)
        {
            UpdateMusicKitMVPState(steamId, validate);
            return;
        }

        Platform::Print("ServerGC: ignoring non protobuf message %u from %llu\n",
            validate.TypeUnmasked(), steamId);
        return;
    }

    // validate the type and contents
    bool isValid = false;
    std::optional<GCMessageWrite> sanitized;

    switch (validate.TypeUnmasked())
    {
    case k_ESOMsg_Create:
    case k_ESOMsg_Update:
    case k_ESOMsg_Destroy:
        isValid = ValidateMessageOwnerSOID<CMsgSOSingleObject>(validate, steamId, sanitized);
        break;

    case k_ESOMsg_CacheSubscribed:
        isValid = ValidateMessageOwnerSOID<CMsgSOCacheSubscribed>(validate, steamId, sanitized);
        break;

    case k_ESOMsg_UpdateMultiple:
        isValid = ValidateMessageOwnerSOID<CMsgSOMultipleObjects>(validate, steamId, sanitized);
        break;

    case k_EMsgGCItemAcknowledged:
        isValid = true;
        break;
    }

    if (!isValid)
    {
        Platform::Print("ServerGC: ignoring net message %u from %llu\n",
            validate.TypeUnmasked(), steamId);
        return;
    }

    // The validated message is parsed again here so the music kit counter can be
    // tracked from the client's own inventory, which is its authoritative copy.
    TrackMusicKitFromNetMessage(steamId, data, size);

    if (!m_sentWelcome)
    {
        // FIXME: ideally we'd sent this on steam logon, instead of on demand...
        Platform::Print("Sending server welcome due to net message\n");
        SendServerWelcome();
    }

    if (sanitized.has_value())
    {
        // pass the sanitized message
        PostToHost(HostEvent::Message, sanitized->TypeMasked(), sanitized->Data(), sanitized->Size());
    }
    else
    {
        // otherwise the old message was fine
        PostToHost(HostEvent::Message, validate.TypeMasked(), data, size);
    }
}

void ServerGC::UpdateMusicKitMVPState(uint64_t steamId, GCMessageRead &messageRead)
{
    uint32_t userId = messageRead.ReadUint32();
    uint32_t hasEquippedStatTrakMusicKit = messageRead.ReadUint32();
    uint32_t currentMVPs = messageRead.ReadUint32();
    if (!messageRead.IsValid() || !userId || hasEquippedStatTrakMusicKit > 1)
    {
        Platform::Print("ServerGC: ignoring malformed music kit MVP state from %llu\n", steamId);
        return;
    }

    bool hasKit = hasEquippedStatTrakMusicKit != 0;

    {
        std::lock_guard<std::mutex> lock{ m_musicKitMVPStateMutex };

        // Preserve the tracked item id so a later item update can still invalidate
        // this value, and so this message cannot clear state for a different kit.
        auto stateIt = m_musicKitMVPStateBySteamId.find(steamId);
        uint64_t itemId = stateIt != m_musicKitMVPStateBySteamId.end() ? stateIt->second.itemId : 0;

        StoreMusicKitStateLocked(steamId, static_cast<int>(userId), itemId, currentMVPs, hasKit);
    }

    Platform::Print("ServerGC: updated music kit MVP state from %llu: userid=%u haskit=%u mvps=%u\n",
        steamId,
        userId,
        hasEquippedStatTrakMusicKit,
        currentMVPs);
}

void ServerGC::SendServerWelcome()
{
    // we don't care about anything in this message, just reply

    CMsgCStrike15Welcome csWelcome;
    csWelcome.set_gscookieid(GameServerCookieId);

    CMsgClientWelcome welcome;
    welcome.set_version(0);
    welcome.set_game_data(csWelcome.SerializeAsString());
    welcome.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));

    GCMessageWrite write{ k_EMsgGCServerWelcome, welcome };
    PostToHost(HostEvent::Message, write.TypeMasked(), write.Data(), write.Size());

    m_sentWelcome = true;
}

void ServerGC::IncrementKillCountAttribute(GCMessageRead &messageRead)
{
    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    // just forward it to the killer
    GCMessageWrite messageWrite{ k_EMsgGC_IncrementKillCountAttribute, message };
    CSteamID killerId{ message.killer_account_id(), k_EUniversePublic, k_EAccountTypeIndividual };
    PostToHost(HostEvent::NetMessage, killerId.ConvertToUint64(), messageWrite.Data(), messageWrite.Size());
}
