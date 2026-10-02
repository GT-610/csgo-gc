#include "stdafx.h"
#include "config.h"
#include "keyvalue.h"
#include "random.h"

constexpr const char *ConfigFilePath = "csgo_gc/config.txt";

const GCConfig &GetConfig()
{
    static GCConfig instance;

    // Publish diagnostics only after construction has finished. Platform::Print
    // calls GetConfig(), so mark them reported before entering the logger.
    static std::atomic<bool> diagnosticsReported{ false };
    bool expected = false;
    if (!diagnosticsReported.load(std::memory_order_relaxed)
        && diagnosticsReported.compare_exchange_strong(expected, true, std::memory_order_relaxed))
    {
        instance.PrintDiagnostics();
    }
    return instance;
}

GCConfig::GCConfig()
{
    KeyValue config{ "config" };

    if (!config.ParseFromFile(ConfigFilePath))
    {
        return;
    }

    m_logOutput = config.GetNumber("log_output", m_logOutput);

    m_appIdOverride = config.GetNumber("appid_override", m_appIdOverride);
    m_showCsgoGCServersOnly = config.GetNumber("show_csgo_gc_servers_only", m_showCsgoGCServersOnly);
    m_overwatchEnabled = config.GetNumber("overwatch_enabled", m_overwatchEnabled);

    const KeyValue *rcon = config.GetSubkey("rcon");
    if (rcon)
    {
        m_rconEnabled = rcon->GetNumber("enabled", m_rconEnabled);
        m_rconBindAddress = rcon->GetString("bind_address", m_rconBindAddress);
        m_rconPort = rcon->GetNumber("port", m_rconPort);
        m_rconPassword = rcon->GetString("password", m_rconPassword);
    }

    const KeyValue *ranks = config.GetSubkey("ranks");
    if (ranks)
    {
        m_competitiveRank = ranks->GetNumber("competitive_rank", m_competitiveRank);
        m_competitiveWins = ranks->GetNumber("competitive_wins", m_competitiveWins);

        m_wingmanRank = ranks->GetNumber("wingman_rank", m_wingmanRank);
        m_wingmanWins = ranks->GetNumber("wingman_wins", m_wingmanWins);

        m_dangerZoneRank = ranks->GetNumber("dangerzone_rank", m_dangerZoneRank);
        m_dangerZoneWins = ranks->GetNumber("dangerzone_wins", m_dangerZoneWins);
    }

    m_destroyUsedItems = config.GetNumber("destroy_used_items", m_destroyUsedItems);
    m_primeStatus = config.GetNumber("prime_status", m_primeStatus);

    std::string_view musicKitStatTrak =
        config.GetString("music_kit_stattrak", std::string_view{});
    if (musicKitStatTrak == "always")
    {
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::Always;
    }
    else if (musicKitStatTrak == "competitive" || musicKitStatTrak.empty())
    {
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::CompetitiveRuleset;
    }
    else
    {
        m_unknownMusicKitStatTrak = musicKitStatTrak;
        m_musicKitStatTrakGate = MusicKit::StatTrakGate::CompetitiveRuleset;
    }

    const KeyValue *rarityWeights = config.GetSubkey("rarity_weights");
    if (rarityWeights)
    {
        m_rarityWeights.clear();
        m_rarityWeights.reserve(rarityWeights->SubkeyCount());

        for (const KeyValue &subkey : *rarityWeights)
        {
            RarityWeight weight;
            weight.rarity = FromString<uint32_t>(subkey.Name());
            weight.weight = FromString<float>(subkey.String());
            m_rarityWeights.push_back(weight);
        }
    }

    m_vacBanned = config.GetNumber("vac_banned", m_vacBanned);
    m_commendedFriendly = config.GetNumber("cmd_friendly", m_commendedFriendly);
    m_commendedTeaching = config.GetNumber("cmd_teaching", m_commendedTeaching);
    m_commendedLeader = config.GetNumber("cmd_leader", m_commendedLeader);
    m_level = config.GetNumber("player_level", m_level);
    m_xp = config.GetNumber("player_cur_xp", m_xp);
}

void GCConfig::PrintDiagnostics() const
{
    if (!m_unknownMusicKitStatTrak.empty())
    {
        Platform::Print("config: unknown music_kit_stattrak value '%s', using competitive\n",
            m_unknownMusicKitStatTrak.c_str());
    }
}

float GCConfig::GetRarityWeight(uint32_t rarity) const
{
    for (const RarityWeight &weight : m_rarityWeights)
    {
        if (weight.rarity == rarity)
        {
            return weight.weight;
        }
    }

    return 0;
}
