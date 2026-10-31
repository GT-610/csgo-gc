#pragma once

#include "gc_const_csgo.h"
#include "item_schema.h" // rarity constants
#include "music_kit.h"

struct RarityWeight
{
    uint32_t rarity;
    float weight;
};

// for Platform::Print calls
enum LogOutput
{
    LogOutputNone, // don't output anything
    LogOutputConsole, // game console
    LogOutputFile // game console and gc_log.txt
};

class GCConfig
{
public:
    GCConfig();

    float GetRarityWeight(uint32_t rarity) const;

    // Log level for Platform::Print.
    LogOutput m_logOutput{ LogOutputConsole };

    // actually default to 4465480 instead of 730, people are going to use old configs
    // and then wonder why the game doesn't work and open an issue on github otherwise
    uint32_t m_appIdOverride{ 4465480 };
    bool m_showCsgoGCServersOnly{ true };
    bool m_overwatchEnabled{ false };
    bool m_rconEnabled{ false };

    // Relax the Panorama code.pbin signature check so a repacked frontend loads.
    // Off by default: it patches the game's own frontend module, which only the
    // client loads, and it is only useful once a modified code.pbin is in place.
    bool m_allowUnsignedPanorama{ false };

    std::string m_rconBindAddress{ "127.0.0.1" };
    uint16_t m_rconPort{ 37016 };
    std::string m_rconPassword;

    // Path the item schema is loaded from. Keep this in sync with the path the
    // game itself reads so item definitions match the running client.
    std::string m_itemSchemaPath{ "csgo/scripts/items/items_game.txt" };

    RankId m_competitiveRank{ RankNone };
    int m_competitiveWins{ 0 };
    RankId m_wingmanRank{ RankNone };
    int m_wingmanWins{ 0 };
    DangerZoneRankId m_dangerZoneRank{ DangerZoneRankNone };
    int m_dangerZoneWins{ 0 };

    bool m_destroyUsedItems{ true };

    // Music kit StatTrak follows competitive rules by default, matching the only
    // scope the official backend ever supported.
    MusicKit::StatTrakGate m_musicKitStatTrakGate{ MusicKit::StatTrakGate::CompetitiveRuleset };

    // accounts default to prime so existing clients keep their current behavior
    bool m_primeStatus{ true };

    bool m_vacBanned{ false };
    int m_commendedFriendly{ 0 };
    int m_commendedTeaching{ 0 };
    int m_commendedLeader{ 0 };
    int m_level{ 0 };
    int m_xp{ 0 };

private:
    friend const GCConfig &GetConfig();
    void PrintDiagnostics() const;
    std::string m_unknownMusicKitStatTrak;

    // default to valve weights
    std::vector<RarityWeight> m_rarityWeights{
        { ItemSchema::RarityCommon, 10000000 },
        { ItemSchema::RarityUncommon, 2000000 },
        { ItemSchema::RarityRare, 400000 },
        { ItemSchema::RarityMythical, 80000 },
        { ItemSchema::RarityLegendary, 16000 },
        { ItemSchema::RarityAncient, 3200 },
        { ItemSchema::RarityUnusual, 1280 },
    };
};

const GCConfig &GetConfig();
