#include "stdafx.h"
#include "bot_skins.h"
#include "item_schema.h"

#include <random>

namespace
{

struct Choice
{
    uint32_t paintKit;
    float minFloat;
    float maxFloat;
};

struct CuratedWeapon
{
    const char *weapon;
    std::vector<const char *> paintKits;
};

// Popular skins per weapon, by their items_game paint kit names. A name the
// schema doesn't have is skipped. Every name here exists for its weapon in the
// final CS:GO items_game.txt.
const std::vector<CuratedWeapon> &Curated()
{
    static const std::vector<CuratedWeapon> curated{
        { "weapon_ak47", { "cu_ak47_cobra", "cu_ak47_asiimov", "cu_ak47_rubber", "cu_fireserpent_ak47_bravo",
            "cu_ak47_anarchy", "gs_ak47_bloodsport", "gs_ak47_empress", "aq_oiled", "gs_ak47_supercharged",
            "cu_ak_neon_rider", "cu_ak_head_shot_holo", "cu_tribute_ak47" } },
        { "weapon_m4a1", { "cu_m4a1_howling", "cu_m4_asimov", "cu_m4a4_neo_noir", "cu_m4a4_desolate_space",
            "gs_m4a4_emperor", "gs_m4a4_sector", "cu_m4a4_ancestral", "cu_poseidon", "cu_m4a4_evil_daimyo" } },
        { "weapon_m4a1_silencer", { "cu_m4a1_hyper_beast", "cu_m4a1s_cyrex", "cu_m4a1s_printstream",
            "gs_m4a1s_snakebite_gold", "cu_m4a1s_soultaker", "gs_m4a1_decimator", "gs_m4a1_mecha_industries",
            "an_red_m4a1s", "cu_m4a1s_nightmare" } },
        { "weapon_awp", { "cu_medieval_dragon_awp", "cu_awp_asimov", "cu_awp_hyper_beast", "cu_awp_neonoir",
            "cu_medusa_awp", "am_lightning_awp", "cu_awp_wildfire", "cu_awp_virus", "gs_awp_gungnir",
            "cu_awp_cobra", "am_awp_glory", "hy_hive" } },
        { "weapon_deagle", { "aa_flames", "cu_deag_printstream", "gs_deagle_aggressor",
            "aq_deserteagle_kumichodragon", "aa_vertigo", "cu_deagle_aureus", "gs_deagle_mecha",
            "cu_desert_eagle_corroden" } },
        { "weapon_usp_silencer", { "cu_usp_kill_confirmed", "cu_usps_noir", "cu_usp_spitfire", "cu_kaiman",
            "cu_usp_cut", "cu_usp_printstream", "aq_usp_stainless", "cu_usp_elegant" } },
        { "weapon_glock", { "aa_fade", "am_dragon_glock", "am_gamma_doppler_phase1_glock", "cu_glock_noir",
            "cu_glock_wasteland_rebel", "am_nuclear_pattern1_glock", "gs_glock18_wrathys", "gs_glock18_award" } },
        { "weapon_hkp2000", { "cu_p2000_fire_elemental", "gs_p2000_imperial_dragon", "aa_fade_metallic",
            "cu_p2000_ivory", "aq_p2000_boom" } },
        { "weapon_p250", { "cu_p250_asiimov", "gs_p250_visions", "gs_p250_cybershell" } },
        { "weapon_fiveseven", { "cu_fiveseven_hyperbeast", "cu_fiveseven_retrobution", "aq_oiled",
            "cu_five_seven_angry", "cu_fiveseven_alpha_omega" } },
        { "weapon_tec9", { "cu_tec9_asiimov", "cu_tec9_avalanche", "gs_tec9_decimator", "cu_tec9_sandstorm" } },
        { "weapon_cz75a", { "cu_cz75_whirlwind", "gs_cz75_tacticat", "cu_cz75a_chastizer" } },
        { "weapon_famas", { "gs_famas_mecha", "cu_famas_pulse", "gs_famas_rapid_eyes" } },
        { "weapon_galilar", { "gs_galil_phoenix", "cu_cerbrus_galil", "gs_galil_nightwing" } },
        { "weapon_aug", { "gs_aug_syd_mead", "cu_aug_chameleonaire", "gs_aug_stymphalian_birds" } },
        { "weapon_sg556", { "cu_sg553_cyrex", "cu_sg553_atlas", "gs_sg553_phantom" } },
        { "weapon_ssg08", { "cu_ssg08_dragonfire_scope", "cu_shark", "cu_ssg08_mainframe", "gs_ssg08_anubis" } },
        { "weapon_scar20", { "cu_scar_cyrex", "gs_scar20_bloodsport", "gs_scar20_powercore" } },
        { "weapon_g3sg1", { "cu_g3sg1_executioner", "gs_g3sg1_flux_purple" } },
        { "weapon_mac10", { "cu_mac10_neonrider", "aa_fade", "gs_mac10_stalker", "cu_korupt" } },
        { "weapon_mp9", { "cu_mp9_hydra", "cu_mp9_food_chain", "gs_mp9_starlight" } },
        { "weapon_mp7", { "gs_mp7_bloodsport", "cu_mp7_nemsis" } },
        { "weapon_mp5sd", { "gs_mp5sd_astromatic" } },
        { "weapon_ump45", { "cu_ump45_primalsaber", "gs_ump_abyss" } },
    };
    return curated;
}

// A colour theme: every weapon a bot with this theme carries (and its knife)
// has a skin in the same colours. Weapons a theme has no skin for keep a
// popular skin from the general list above.
struct ThemedWeapon
{
    const char *weapon;
    std::vector<const char *> paintKits;
};

struct ThemeDef
{
    const char *name;
    std::vector<const char *> knifeFinishes; // finishes that exist on every core knife
    std::vector<ThemedWeapon> weapons;
};

const std::vector<ThemeDef> &Themes()
{
    static const std::vector<ThemeDef> themes{
        { "Red",
            { "am_ruby_marbleized", "hy_webs", "am_marble_fade" },
            {
                { "weapon_ak47", { "cu_ak47_cobra", "cu_fireserpent_ak47_bravo" } },
                { "weapon_m4a1", { "cu_m4a4_hellfire", "cu_m4a1_howling" } },
                { "weapon_m4a1_silencer", { "an_red_m4a1s", "cu_m4a1s_soultaker" } },
                { "weapon_awp", { "cu_awp_cobra", "cu_awp_wildfire" } },
                { "weapon_deagle", { "aa_flames", "gs_deagle_aggressor" } },
                { "weapon_usp_silencer", { "cu_usp_kill_confirmed", "am_electric_red" } },
                { "weapon_glock", { "so_red", "cu_glock_wasteland_rebel" } },
                { "weapon_hkp2000", { "cu_p2000_fire_elemental", "am_p2000_imperial_red" } },
            } },
        { "Blue",
            { "aq_blued", "am_doppler_phase4", "am_doppler_phase1" },
            {
                { "weapon_ak47", { "hy_ak47lam_blue", "am_aqua_flecks" } },
                { "weapon_m4a1", { "cu_m4a4_desolate_space" } },
                { "weapon_m4a1_silencer", { "am_m4a1s_bluesmoke" } },
                { "weapon_awp", { "am_tigers_blue", "am_awp_glory" } },
                { "weapon_deagle", { "aq_deagle_naga", "sp_spacerace_blue" } },
                { "weapon_usp_silencer", { "hy_indigo_usp", "cu_usp_cyrex" } },
                { "weapon_glock", { "aa_vertigo_blue", "aq_glock18_flames_blue" } },
                { "weapon_hkp2000", { "am_ossify_blue_p2000_bravo" } },
            } },
        { "Green",
            { "am_emerald_marbleized", "am_gamma_doppler_phase1", "am_gamma_doppler_phase3", "hy_forest_boreal" },
            {
                { "weapon_ak47", { "cu_green_leather_ak47", "am_bamboo_jungle", "cu_ak_island_floral" } },
                { "weapon_m4a1_silencer", { "am_crystallized_dark_green", "hy_forest_boreal" } },
                { "weapon_awp", { "cu_awp_virus", "hy_snakeskin" } },
                { "weapon_deagle", { "am_jorm_green" } },
                { "weapon_usp_silencer", { "cu_kaiman", "sp_leaves", "so_khaki_green" } },
                { "weapon_glock", { "am_emerald_marbleized_glock", "am_gamma_doppler_phase1_glock" } },
                { "weapon_hkp2000", { "sp_leaves_grassland", "so_grassland" } },
            } },
        { "Purple and Pink",
            { "aa_fade", "am_marble_fade" },
            {
                { "weapon_ak47", { "cu_ak_neon_rider", "am_fuschia", "hy_veneto_purple" } },
                { "weapon_awp", { "cu_awp_chroma_pink", "hy_ddpat_pink" } },
                { "weapon_deagle", { "am_heist_plans_purple" } },
                { "weapon_usp_silencer", { "am_ddpat_purple", "am_intelligence_magenta" } },
                { "weapon_glock", { "hy_ddpat_pink" } },
            } },
        { "Gold and Orange",
            { "an_tiger_orange", "aq_oiled" },
            {
                { "weapon_ak47", { "gs_ak47_gold_arabesque", "cu_ak47_asiimov", "cu_ak_jaguar" } },
                { "weapon_m4a1", { "cu_m4_asimov" } },
                { "weapon_m4a1_silencer", { "gs_m4a1s_snakebite_gold" } },
                { "weapon_awp", { "cu_awp_asimov", "cu_awp_wildfire" } },
                { "weapon_deagle", { "am_gold_brick", "aa_flames" } },
            } },
        { "Black and White",
            { "so_night", "aq_steel_knife" },
            {
                { "weapon_ak47", { "hy_ak47lam_bw", "cu_ak_xray" } },
                { "weapon_m4a1", { "cu_xray_m4" } },
                { "weapon_deagle", { "so_night" } },
                { "weapon_usp_silencer", { "cu_usps_noir", "so_whiteout_riptide" } },
                { "weapon_glock", { "so_night", "cu_glock_noir" } },
            } },
    };
    return themes;
}

// the knives a bot can carry instead of its default one (they all have the
// finishes above)
constexpr const char *KnifeClasses[] = {
    "weapon_knife_karambit", "weapon_bayonet", "weapon_knife_m9_bayonet", "weapon_knife_flip",
    "weapon_knife_gut", "weapon_knife_butterfly", "weapon_knife_tactical", "weapon_knife_falchion",
    "weapon_knife_push", "weapon_knife_survival_bowie",
};

struct ResolvedTheme
{
    std::unordered_map<std::string, std::vector<Choice>> weapons;
    std::vector<Choice> knifeFinishes;
};

std::vector<ResolvedTheme> s_themes;
std::unordered_map<std::string, std::vector<Choice>> s_choices;
bool s_built;
uint64_t s_matchSeed = 0x9e3779b97f4a7c15ull;

// wear ranges of the five exteriors and how likely bots get each
struct Exterior
{
    float low;
    float high;
    float weight;
};

constexpr Exterior Exteriors[] = {
    { 0.00f, 0.07f, 30 }, // Factory New
    { 0.07f, 0.15f, 35 }, // Minimal Wear
    { 0.15f, 0.38f, 25 }, // Field-Tested
    { 0.38f, 0.45f, 5 }, // Well-Worn
    { 0.45f, 1.00f, 5 }, // Battle-Scarred
};

} // namespace

void BotSkins::Build(const ItemSchema &schema)
{
    s_choices.clear();

    for (const CuratedWeapon &weapon : Curated())
    {
        std::vector<Choice> &choices = s_choices[weapon.weapon];
        for (const char *name : weapon.paintKits)
        {
            const PaintKitInfo *kit = schema.FindPaintKitByName(name);
            if (kit)
            {
                choices.push_back({ kit->m_defIndex, kit->m_minFloat, kit->m_maxFloat });
            }
        }
    }

    // everything else gets a random Rare-or-better skin that exists for it
    constexpr uint32_t Rarities[] = {
        ItemSchema::RarityRare, ItemSchema::RarityMythical, ItemSchema::RarityLegendary, ItemSchema::RarityAncient
    };
    for (uint32_t rarity : Rarities)
    {
        std::vector<const LootListItem *> candidates;
        if (!schema.GetDropSkinCandidates(rarity, candidates))
        {
            continue;
        }

        for (const LootListItem *candidate : candidates)
        {
            if (!candidate->itemInfo || !candidate->paintKitInfo)
            {
                continue;
            }

            std::vector<Choice> &choices = s_choices[candidate->itemInfo->m_name];
            // curated weapons keep only their curated list
            const bool curated = std::any_of(Curated().begin(), Curated().end(),
                [&](const CuratedWeapon &weapon) { return candidate->itemInfo->m_name == weapon.weapon; });
            if (!curated)
            {
                choices.push_back({ candidate->paintKitInfo->m_defIndex,
                    candidate->paintKitInfo->m_minFloat, candidate->paintKitInfo->m_maxFloat });
            }
        }
    }

    s_themes.clear();
    for (const ThemeDef &theme : Themes())
    {
        ResolvedTheme &resolved = s_themes.emplace_back();
        for (const char *name : theme.knifeFinishes)
        {
            if (const PaintKitInfo *kit = schema.FindPaintKitByName(name))
            {
                resolved.knifeFinishes.push_back({ kit->m_defIndex, kit->m_minFloat, kit->m_maxFloat });
            }
        }

        for (const ThemedWeapon &weapon : theme.weapons)
        {
            for (const char *name : weapon.paintKits)
            {
                if (const PaintKitInfo *kit = schema.FindPaintKitByName(name))
                {
                    resolved.weapons[weapon.weapon].push_back({ kit->m_defIndex, kit->m_minFloat, kit->m_maxFloat });
                }
            }
        }
    }

    s_built = true;
}

bool BotSkins::IsBuilt()
{
    return s_built;
}

void BotSkins::NewMatch()
{
    std::random_device device;
    s_matchSeed = (static_cast<uint64_t>(device()) << 32) ^ device();
}

namespace
{

bool IsKnife(std::string_view name)
{
    return name.rfind("weapon_knife", 0) == 0 || name == "weapon_bayonet";
}

std::mt19937 MakeRandom(uint64_t botKey, std::string_view salt)
{
    std::seed_seq seed{
        static_cast<uint32_t>(s_matchSeed), static_cast<uint32_t>(s_matchSeed >> 32),
        static_cast<uint32_t>(botKey), static_cast<uint32_t>(botKey >> 32),
        static_cast<uint32_t>(std::hash<std::string_view>{}(salt))
    };
    return std::mt19937{ seed };
}

const ResolvedTheme *ThemeFor(uint64_t botKey)
{
    if (s_themes.empty())
    {
        return nullptr;
    }

    std::mt19937 random = MakeRandom(botKey, "theme");
    return &s_themes[random() % s_themes.size()];
}

} // namespace

const char *BotSkins::KnifeFor(std::string_view weaponName, uint64_t botKey)
{
    // only a bot's plain starting knife is swapped
    if (!s_built || (weaponName != "weapon_knife" && weaponName != "weapon_knife_t"))
    {
        return nullptr;
    }

    const ResolvedTheme *theme = ThemeFor(botKey);
    if (!theme || theme->knifeFinishes.empty())
    {
        return nullptr;
    }

    std::mt19937 random = MakeRandom(botKey, "knife");
    return KnifeClasses[random() % std::size(KnifeClasses)];
}

const char *BotSkins::ThemeName(uint64_t botKey)
{
    if (!s_built || s_themes.empty())
    {
        return "";
    }

    const ResolvedTheme *theme = ThemeFor(botKey);
    return Themes()[static_cast<size_t>(theme - s_themes.data())].name;
}

bool BotSkins::Pick(std::string_view weaponName, uint64_t botKey, Skin &skin)
{
    const ResolvedTheme *theme = ThemeFor(botKey);

    const std::vector<Choice> *choices = nullptr;
    if (IsKnife(weaponName))
    {
        if (theme && !theme->knifeFinishes.empty())
        {
            choices = &theme->knifeFinishes;
        }
    }
    else
    {
        if (theme)
        {
            auto themed = theme->weapons.find(std::string{ weaponName });
            if (themed != theme->weapons.end() && !themed->second.empty())
            {
                choices = &themed->second;
            }
        }

        if (!choices)
        {
            auto general = s_choices.find(std::string{ weaponName });
            if (general != s_choices.end() && !general->second.empty())
            {
                choices = &general->second;
            }
        }
    }

    if (!choices)
    {
        return false;
    }

    // the same bot gets the same skin for a weapon for the whole match
    std::mt19937 random = MakeRandom(botKey, weaponName);
    const Choice &choice = (*choices)[random() % choices->size()];

    float totalWeight = 0;
    for (const Exterior &exterior : Exteriors)
    {
        totalWeight += exterior.weight;
    }
    float roll = std::uniform_real_distribution<float>{ 0.0f, totalWeight }(random);
    const Exterior *picked = &Exteriors[0];
    for (const Exterior &exterior : Exteriors)
    {
        picked = &exterior;
        if (roll < exterior.weight)
        {
            break;
        }
        roll -= exterior.weight;
    }

    float wear = std::uniform_real_distribution<float>{ picked->low, picked->high }(random);
    wear = std::clamp(wear, choice.minFloat, choice.maxFloat);

    skin.paintKit = choice.paintKit;
    skin.seed = random() % 1000;
    skin.wear = wear;
    return true;
}
