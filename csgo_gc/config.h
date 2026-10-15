#pragma once

#include "gc_const_csgo.h"
#include "item_schema.h" // rarity constants
#include "music_kit.h"

struct RarityWeight
{
    uint32_t rarity;
    float weight;
};

// case opening pity: the gold chance climbs after softStart gold-less cases
// and the guaranteedAt-th gold-less case is always a gold
struct CasePityConfig
{
    bool enabled{ true };
    uint32_t softStart{ 25 };
    uint32_t guaranteedAt{ 50 };
    // while waiting for a gold, Restricted/Classified/Covert get more likely:
    // their weight is multiplied by up to this much, climbing steadily from
    // the first gold-less case to guaranteed_at (1 = no boost)
    std::vector<RarityWeight> rarityBoost{
        { 4, 3.0f }, // restricted (purple)
        { 5, 5.0f }, // classified (pink)
        { 6, 8.0f }, // covert (red)
    };
};

// end-of-match item drops, modeled on old CS:GO weekly drops
struct MatchDropConfig
{
    bool enabled{ true };
    float dropChance{ 1.0f }; // chance of a drop when a match ends
    float caseChance{ 0.35f }; // chance that a drop is a case rather than a skin
    uint32_t dropsPerMatch{ 1 }; // separate drops rolled when a match ends
    // skin drop rarity weights (old CS:GO drops were almost always Consumer/Industrial)
    std::vector<RarityWeight> skinRarityWeights{
        { ItemSchema::RarityCommon, 45 },
        { ItemSchema::RarityUncommon, 30 },
        { ItemSchema::RarityRare, 17 },
        { ItemSchema::RarityMythical, 6 },
        { ItemSchema::RarityLegendary, 1.5f },
        { ItemSchema::RarityAncient, 0.5f },
    };
};

// stars earned from playing, spent in the Case Store. a case (or other
// container) costs casePrice stars with its key included
struct StarShopConfig
{
    bool enabled{ true };
    uint32_t casePrice{ 10 };
    uint32_t capsulePrice{ 5 };
    uint32_t souvenirPrice{ 25 };
    uint32_t customCasePrice{ 100 }; // cases from custom_cases.txt
    uint32_t agentPrice{ 50 }; // a random agent
    // how many of each the star store sells, newest first (0 = all of them).
    // the rest can only be found through match drops
    uint32_t maxCases{ 12 };
    uint32_t maxCapsules{ 10 };
    uint32_t maxSouvenirPackages{ 0 };
    // make the game think the operation is running so its page shows
    // (moves quest_schedule's start in items_game.txt)
    bool operationActive{ true };
    uint32_t perKill{ 1 };
    uint32_t perHeadshot{ 1 }; // on top of the kill
    uint32_t perMvp{ 3 };
    uint32_t perPlant{ 2 };
    uint32_t perDefuse{ 3 };
    uint32_t perMatch{ 5 }; // finishing a match
};

// random popular skins for bots, set on their weapons by a hook in server.dll
// (listen servers, i.e. bot matches started from the game; experimental)
struct BotSkinsConfig
{
    bool enabled{ false };
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

    // options used by platform layer (bruh)
    LogOutput GetLogOutput() const { return m_logOutput; }

    // options used by steam hook
    uint32_t AppIdOverride() const { return m_appIdOverride; }
    bool ShowCsgoGCServersOnly() const { return m_showCsgoGCServersOnly; }
    bool OverwatchEnabled() const { return m_overwatchEnabled; }
    bool RconEnabled() const { return m_rconEnabled; }
    const std::string &RconBindAddress() const { return m_rconBindAddress; }
    uint16_t RconPort() const { return m_rconPort; }
    const std::string &RconPassword() const { return m_rconPassword; }

    RankId CompetitiveRank() const { return m_competitiveRank; }
    int CompetitiveWins() const { return m_competitiveWins; }
    RankId WingmanRank() const { return m_wingmanRank; }
    int WingmanWins() const { return m_wingmanWins; }
    DangerZoneRankId DangerZoneRank() const { return m_dangerZoneRank; }
    int DangerZoneWins() const { return m_dangerZoneWins; }

    bool DestroyUsedItems() const { return m_destroyUsedItems; }
    bool StoreAllCases() const { return m_storeAllCases; }
    bool StoreWeaponCases() const { return m_storeWeaponCases; }
    bool StoreSouvenirPackages() const { return m_storeSouvenirPackages; }
    bool StoreCapsules() const { return m_storeCapsules; }
    bool StoreOtherContainers() const { return m_storeOtherContainers; }
    bool StoreHideIndividualItems() const { return m_storeHideIndividualItems; }
    bool CustomCasePreviews() const { return m_customCasePreviews; }
    bool GoldTradeUpRecipes() const { return m_goldTradeUpRecipes; }
    bool CaseStoreMenu() const { return m_caseStoreMenu; }
    bool PanoramaDebug() const { return m_panoramaDebug; }
    float ViewmodelRange() const { return m_viewmodelRange; }
    bool TradeUpMixStatTrak() const { return m_tradeUpMixStatTrak; }

    MusicKit::StatTrakGate MusicKitStatTrakGate() const { return m_musicKitStatTrakGate; }

    // prime status shown to the game client, frozen at the configured value
    bool PrimeStatus() const { return m_primeStatus; }

    bool AccountStatus() const { return m_vacBanned; }
    int CommendedFriendly() const { return m_commendedFriendly; }
    int CommendedTeaching() const { return m_commendedTeaching; }
    int CommendedLeader() const { return m_commendedLeader; }
    int Level() const { return m_level; }
    int Xp() const { return m_xp; }

    float GetRarityWeight(uint32_t rarity) const;

    const CasePityConfig &CasePity() const { return m_casePity; }
    const MatchDropConfig &MatchDrops() const { return m_matchDrops; }
    const StarShopConfig &StarShop() const { return m_starShop; }
    const BotSkinsConfig &BotSkins() const { return m_botSkins; }
    bool ItemDeleteMenu() const { return m_itemDeleteMenu; }

private:
    LogOutput m_logOutput{ LogOutputConsole };

    // actually default to 4465480 instead of 730, people are going to use old configs
    // and then wonder why the game doesn't work and open an issue on github otherwise
    uint32_t m_appIdOverride{ 4465480 };
    bool m_showCsgoGCServersOnly{ true };
    bool m_overwatchEnabled{ false };
    bool m_rconEnabled{ false };
    std::string m_rconBindAddress{ "127.0.0.1" };
    uint16_t m_rconPort{ 37016 };
    std::string m_rconPassword;

    RankId m_competitiveRank{ RankNone };
    int m_competitiveWins{ 0 };
    RankId m_wingmanRank{ RankNone };
    int m_wingmanWins{ 0 };
    DangerZoneRankId m_dangerZoneRank{ DangerZoneRankNone };
    int m_dangerZoneWins{ 0 };

    bool m_destroyUsedItems{ true };
    bool m_storeAllCases{ true };
    bool m_storeWeaponCases{ true };
    bool m_storeSouvenirPackages{ true };
    bool m_storeCapsules{ true };
    bool m_storeOtherContainers{ false };
    bool m_storeHideIndividualItems{ true };
    bool m_customCasePreviews{ false };
    bool m_goldTradeUpRecipes{ true };
    bool m_caseStoreMenu{ true };
    bool m_panoramaDebug{ false };
    float m_viewmodelRange{ 1.0f };
    bool m_tradeUpMixStatTrak{ true };
    bool m_itemDeleteMenu{ true };

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

    CasePityConfig m_casePity;
    MatchDropConfig m_matchDrops;
    StarShopConfig m_starShop;
    BotSkinsConfig m_botSkins;
};

const GCConfig &GetConfig();
