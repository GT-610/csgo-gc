#pragma once

#include "gc_shared.h"
#include "music_kit.h"

class ServerGC final : public SharedGC
{
public:
    ServerGC();
    ~ServerGC();

    // The music kit MVP count to publish for this player in their round_mvp event,
    // or false when nothing should be published. Returns false when the StatTrak
    // gate is inactive for the current game type and mode, so a round_mvp event
    // outside a competitive ruleset carries no music kit data at all.
    bool RoundMVPMusicKitCountForUserId(int userId, int &musickitmvps) const;

private:
    void HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer) override;

    // event handlers
    void HandleMessage(uint32_t type, const void *data, uint32_t size);
    void HandleNetMessage(uint64_t steamId, const void *data, uint32_t size);
    void HandleClientSOCacheUnsubscribe(uint64_t steamId);

    void SendServerWelcome();
    void IncrementKillCountAttribute(GCMessageRead &messageRead);
    void UpdateMusicKitMVPState(uint64_t steamId, GCMessageRead &messageRead);

    // Tracks the music kit counter carried by the client's own SO cache messages.
    // The client's inventory is the authoritative copy of its music kit, so the
    // server can publish a count even when the client never sends one itself.
    void TrackMusicKitFromNetMessage(uint64_t steamId, const void *data, uint32_t size);
    void TrackMusicKitFromCache(uint64_t steamId, const CMsgSOCacheSubscribed &message);
    void TrackMusicKitFromItem(uint64_t steamId, const CSOEconItem &item);

    // Stores a player's music kit state, keeping both indexes consistent. An
    // absent StatTrak music kit removes the player entirely. Callers must hold the
    // mutex.
    void StoreMusicKitStateLocked(uint64_t steamId, int userId, uint64_t itemId,
        uint32_t currentMVPs, bool hasEquippedStatTrakMusicKit);

    // Drops all tracked state for a player. Callers must hold the mutex.
    void ForgetMusicKitStateLocked(uint64_t steamId);

    bool m_sentWelcome{};

    struct MusicKitMVPState
    {
        int userId{};
        uint32_t currentMVPs{};
        bool hasEquippedStatTrakMusicKit{};

        // Item id whose counter currentMVPs came from, so that unequipping or
        // replacing that specific item can invalidate the tracked value without an
        // unrelated item's update clearing it.
        uint64_t itemId{};
    };

    mutable std::mutex m_musicKitMVPStateMutex;
    std::unordered_map<uint64_t, MusicKitMVPState> m_musicKitMVPStateBySteamId;
    std::unordered_map<int, MusicKitMVPState> m_musicKitMVPStateByUserId;
};
