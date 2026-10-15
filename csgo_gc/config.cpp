#include "stdafx.h"
#include "config.h"
#include "keyvalue.h"
#include "random.h"

constexpr const char *ConfigFilePath = "csgo_gc/config.txt";

const GCConfig &GetConfig()
{
    static GCConfig instance;
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
    m_storeAllCases = config.GetNumber("store_all_cases", m_storeAllCases);
    m_storeHideIndividualItems = config.GetNumber("store_hide_individual_items", m_storeHideIndividualItems);
    if (const KeyValue *storeContainers = config.GetSubkey("store_containers"))
    {
        m_storeWeaponCases = storeContainers->GetNumber("weapon_cases", m_storeWeaponCases);
        m_storeSouvenirPackages = storeContainers->GetNumber("souvenir_packages", m_storeSouvenirPackages);
        m_storeCapsules = storeContainers->GetNumber("capsules", m_storeCapsules);
        m_storeOtherContainers = storeContainers->GetNumber("other", m_storeOtherContainers);
    }
    m_customCasePreviews = config.GetNumber("custom_case_previews", m_customCasePreviews);
    m_goldTradeUpRecipes = config.GetNumber("gold_tradeup_recipes", m_goldTradeUpRecipes);
    m_careerTracking = config.GetNumber("career_tracking", m_careerTracking);
    m_caseStoreMenu = config.GetNumber("case_store_menu", m_caseStoreMenu);
    m_panoramaDebug = config.GetNumber("panorama_debug", m_panoramaDebug);
    m_viewmodelRange = std::clamp(config.GetNumber("viewmodel_range", m_viewmodelRange), 1.0f, 100.0f);
    m_tradeUpMixStatTrak = config.GetNumber("tradeup_mix_stattrak", m_tradeUpMixStatTrak);
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
        Platform::Print("config: unknown music_kit_stattrak value '%.*s', using competitive\n",
            static_cast<int>(musicKitStatTrak.size()),
            musicKitStatTrak.data());
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

    const KeyValue *casePity = config.GetSubkey("case_pity");
    if (casePity)
    {
        m_casePity.enabled = casePity->GetNumber("enabled", m_casePity.enabled);
        m_casePity.softStart = casePity->GetNumber("soft_pity_start", m_casePity.softStart);
        m_casePity.guaranteedAt = casePity->GetNumber("guaranteed_at", m_casePity.guaranteedAt);
        if (m_casePity.guaranteedAt < 1)
        {
            m_casePity.guaranteedAt = 1;
        }
        if (m_casePity.softStart >= m_casePity.guaranteedAt)
        {
            m_casePity.softStart = m_casePity.guaranteedAt - 1;
        }

        if (const KeyValue *boost = casePity->GetSubkey("rarity_boost"))
        {
            m_casePity.rarityBoost.clear();
            for (const KeyValue &subkey : *boost)
            {
                RarityWeight weight;
                weight.rarity = FromString<uint32_t>(subkey.Name());
                weight.weight = std::max(FromString<float>(subkey.String()), 0.0f);
                m_casePity.rarityBoost.push_back(weight);
            }
        }
    }

    const KeyValue *matchDrops = config.GetSubkey("match_drops");
    if (matchDrops)
    {
        m_matchDrops.enabled = matchDrops->GetNumber("enabled", m_matchDrops.enabled);
        m_matchDrops.dropChance = std::clamp(matchDrops->GetNumber("drop_chance", m_matchDrops.dropChance), 0.0f, 1.0f);
        m_matchDrops.caseChance = std::clamp(matchDrops->GetNumber("case_chance", m_matchDrops.caseChance), 0.0f, 1.0f);
        m_matchDrops.dropsPerMatch = std::clamp<uint32_t>(matchDrops->GetNumber("drops_per_match", m_matchDrops.dropsPerMatch), 1, 20);

        const KeyValue *skinWeights = matchDrops->GetSubkey("skin_rarity_weights");
        if (skinWeights)
        {
            m_matchDrops.skinRarityWeights.clear();
            for (const KeyValue &subkey : *skinWeights)
            {
                RarityWeight weight;
                weight.rarity = FromString<uint32_t>(subkey.Name());
                weight.weight = FromString<float>(subkey.String());
                m_matchDrops.skinRarityWeights.push_back(weight);
            }
        }
    }

    m_itemDeleteMenu = config.GetNumber("item_delete_menu", m_itemDeleteMenu);

    const KeyValue *starShop = config.GetSubkey("star_shop");
    if (starShop)
    {
        m_starShop.enabled = starShop->GetNumber("enabled", m_starShop.enabled);
        m_starShop.casePrice = starShop->GetNumber("case_price", m_starShop.casePrice);
        m_starShop.capsulePrice = starShop->GetNumber("capsule_price", m_starShop.capsulePrice);
        m_starShop.souvenirPrice = starShop->GetNumber("souvenir_price", m_starShop.souvenirPrice);
        m_starShop.customCasePrice = starShop->GetNumber("custom_case_price", m_starShop.customCasePrice);
        m_starShop.agentPrice = starShop->GetNumber("agent_price", m_starShop.agentPrice);
        m_starShop.operationActive = starShop->GetNumber("operation_active", m_starShop.operationActive);
        if (const KeyValue *sell = starShop->GetSubkey("sell"))
        {
            m_starShop.sellEnabled = sell->GetNumber("enabled", m_starShop.sellEnabled);
            m_starShop.sellConsumer = sell->GetNumber("consumer", m_starShop.sellConsumer);
            m_starShop.sellIndustrial = sell->GetNumber("industrial", m_starShop.sellIndustrial);
            m_starShop.sellMilSpec = sell->GetNumber("mil_spec", m_starShop.sellMilSpec);
            m_starShop.sellRestricted = sell->GetNumber("restricted", m_starShop.sellRestricted);
            m_starShop.sellClassified = sell->GetNumber("classified", m_starShop.sellClassified);
            m_starShop.sellCovert = sell->GetNumber("covert", m_starShop.sellCovert);
            m_starShop.sellKnifeOrGlove = sell->GetNumber("knife_glove", m_starShop.sellKnifeOrGlove);
        }
        m_starShop.maxCases = starShop->GetNumber("max_cases", m_starShop.maxCases);
        m_starShop.maxCapsules = starShop->GetNumber("max_capsules", m_starShop.maxCapsules);
        m_starShop.shopCapsules = starShop->GetNumber("shop_capsules", m_starShop.shopCapsules);
        m_starShop.maxSouvenirPackages = starShop->GetNumber("max_souvenir_packages", m_starShop.maxSouvenirPackages);
        m_starShop.perKill = starShop->GetNumber("kill", m_starShop.perKill);
        m_starShop.perHeadshot = starShop->GetNumber("headshot", m_starShop.perHeadshot);
        m_starShop.perMvp = starShop->GetNumber("mvp", m_starShop.perMvp);
        m_starShop.perPlant = starShop->GetNumber("plant", m_starShop.perPlant);
        m_starShop.perDefuse = starShop->GetNumber("defuse", m_starShop.perDefuse);
        m_starShop.perMatch = starShop->GetNumber("match_complete", m_starShop.perMatch);
    }

    const KeyValue *botSkins = config.GetSubkey("bot_skins");
    if (botSkins)
    {
        m_botSkins.enabled = botSkins->GetNumber("enabled", m_botSkins.enabled);
        m_botSkins.knives = botSkins->GetNumber("knives", m_botSkins.knives);
    }

    m_vacBanned = config.GetNumber("vac_banned", m_vacBanned);
    m_commendedFriendly = config.GetNumber("cmd_friendly", m_commendedFriendly);
    m_commendedTeaching = config.GetNumber("cmd_teaching", m_commendedTeaching);
    m_commendedLeader = config.GetNumber("cmd_leader", m_commendedLeader);
    m_level = config.GetNumber("player_level", m_level);
    m_xp = config.GetNumber("player_cur_xp", m_xp);
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
