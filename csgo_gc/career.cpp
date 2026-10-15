#include "stdafx.h"
#include "career.h"
#include "item_schema.h"
#include "item_utils.h"
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_set>

namespace
{

struct AchievementDef
{
    const char *id;
    const char *title;
    const char *desc;
    const char *stat;     // the counter it watches
    uint64_t threshold;
    uint32_t stars;
    uint32_t category; // index into AchievementCategories
};

// one table: a counter reaching a number. Counters that aren't plain event counts (gold_cases, cases_distinct,
// skins_found, the knife and glove type counts, the streaks, goals_done) are kept up to date by the career itself
constexpr const char *AchievementCategories[] = { "Cases", "Pulls", "Gold", "Knives and Gloves", "Collecting", "Trade-ups", "Matches", "Stars" };
constexpr AchievementDef Achievements[] = {
    { "first_case", "First Crack", "Open your first case.", "cases_opened", 1, 100, 0 },
    { "cases_10", "Warming Up", "Open 10 cases.", "cases_opened", 10, 250, 0 },
    { "cases_opened_25", "Getting Hooked", "Open 25 cases.", "cases_opened", 25, 350, 0 },
    { "cases_opened_50", "Key Collector", "Open 50 cases.", "cases_opened", 50, 600, 0 },
    { "cases_100", "Case Hardened", "Open 100 cases.", "cases_opened", 100, 1000, 0 },
    { "cases_opened_250", "Crate Digger", "Open 250 cases.", "cases_opened", 250, 1600, 0 },
    { "cases_500", "Unboxing Addict", "Open 500 cases.", "cases_opened", 500, 3000, 0 },
    { "cases_1000", "Thousand Keys", "Open 1,000 cases.", "cases_opened", 1000, 6000, 0 },
    { "cases_opened_2500", "Case Baron", "Open 2,500 cases.", "cases_opened", 2500, 6500, 0 },
    { "cases_opened_5000", "Keymaster", "Open 5,000 cases.", "cases_opened", 5000, 10000, 0 },
    { "custom_10", "House Brand", "Open 10 custom cases.", "custom_cases_opened", 10, 500, 0 },
    { "custom_cases_opened_50", "Regular", "Open 50 custom cases.", "custom_cases_opened", 50, 350, 0 },
    { "custom_100", "Loyal Customer", "Open 100 custom cases.", "custom_cases_opened", 100, 2500, 0 },
    { "custom_cases_opened_250", "Shop Local", "Open 250 custom cases.", "custom_cases_opened", 250, 1600, 0 },
    { "custom_cases_opened_500", "Custom Fanatic", "Open 500 custom cases.", "custom_cases_opened", 500, 4000, 0 },
    { "custom_cases_opened_1000", "Home Turf", "Open 1,000 custom cases.", "custom_cases_opened", 1000, 10000, 0 },
    { "cases_distinct_5", "Sampler", "Open 5 different cases.", "cases_distinct", 5, 100, 0 },
    { "cases_distinct_10", "Variety Pack", "Open 10 different cases.", "cases_distinct", 10, 400, 0 },
    { "cases_distinct_20", "Well Traveled", "Open 20 different cases.", "cases_distinct", 20, 1200, 0 },
    { "cases_distinct_30", "Connoisseur", "Open 30 different cases.", "cases_distinct", 30, 1900, 0 },
    { "cases_distinct_50", "Case Cartographer", "Open 50 different cases.", "cases_distinct", 50, 4800, 0 },
    { "cases_distinct_75", "Full Menu", "Open 75 different cases.", "cases_distinct", 75, 12000, 0 },
    { "other_opened_1", "Sticker Shock", "Open 1 sticker capsule or other containers.", "other_opened", 1, 50, 0 },
    { "other_opened_10", "Capsule Corp", "Open 10 sticker capsules or other containers.", "other_opened", 10, 400, 0 },
    { "other_opened_50", "Peeling Out", "Open 50 sticker capsules or other containers.", "other_opened", 50, 1750, 0 },
    { "other_opened_100", "Sticker Bomber", "Open 100 sticker capsules or other containers.", "other_opened", 100, 7000, 0 },
    { "pulls_milspec_10", "Bread and Butter", "Pull 10 Mil-Spec skins from cases.", "pulls_milspec", 10, 50, 1 },
    { "pulls_milspec_100", "Mil-Spec Mogul", "Pull 100 Mil-Spec skins from cases.", "pulls_milspec", 100, 600, 1 },
    { "pulls_milspec_500", "Blue Wave", "Pull 500 Mil-Spec skins from cases.", "pulls_milspec", 500, 6000, 1 },
    { "pulls_restricted_10", "Purple Patch", "Pull 10 Restricted skins from cases.", "pulls_restricted", 10, 100, 1 },
    { "pulls_restricted_50", "Grape Escape", "Pull 50 Restricted skins from cases.", "pulls_restricted", 50, 800, 1 },
    { "pulls_restricted_250", "Royal Flush", "Pull 250 Restricted skins from cases.", "pulls_restricted", 250, 8000, 1 },
    { "pulls_classified_1", "Pink Slip", "Pull 1 Classified skins from cases.", "pulls_classified", 1, 100, 1 },
    { "pulls_classified_10", "In the Pink", "Pull 10 Classified skins from cases.", "pulls_classified", 10, 350, 1 },
    { "pulls_classified_50", "Pretty in Pink", "Pull 50 Classified skins from cases.", "pulls_classified", 50, 1000, 1 },
    { "pulls_classified_100", "Pink Panther", "Pull 100 Classified skins from cases.", "pulls_classified", 100, 4000, 1 },
    { "pulls_classified_250", "Think Pink", "Pull 250 Classified skins from cases.", "pulls_classified", 250, 10000, 1 },
    { "covert_first", "Red Alert", "Pull 1 Covert skins from cases.", "pulls_covert", 1, 300, 1 },
    { "pulls_covert_5", "Red Flag", "Pull 5 Covert skins from cases.", "pulls_covert", 5, 400, 1 },
    { "pulls_covert_10", "Red Handed", "Pull 10 Covert skins from cases.", "pulls_covert", 10, 700, 1 },
    { "covert_25", "Seeing Red", "Pull 25 Covert skins from cases.", "pulls_covert", 25, 1500, 1 },
    { "pulls_covert_50", "Code Red", "Pull 50 Covert skins from cases.", "pulls_covert", 50, 3000, 1 },
    { "pulls_covert_100", "Crimson Tide", "Pull 100 Covert skins from cases.", "pulls_covert", 100, 7800, 1 },
    { "pulls_covert_250", "Scarlet Letter", "Pull 250 Covert skins from cases.", "pulls_covert", 250, 12000, 1 },
    { "stattrak_first", "Counting Kills", "Pull 1 StatTrak skins from cases.", "stattrak_pulls", 1, 200, 1 },
    { "stattrak_pulls_10", "Odometer", "Pull 10 StatTrak skins from cases.", "stattrak_pulls", 10, 350, 1 },
    { "stattrak_pulls_25", "Kill Counter", "Pull 25 StatTrak skins from cases.", "stattrak_pulls", 25, 1000, 1 },
    { "stattrak_pulls_50", "Tally Marks", "Pull 50 StatTrak skins from cases.", "stattrak_pulls", 50, 1600, 1 },
    { "stattrak_pulls_100", "Scoreboard", "Pull 100 StatTrak skins from cases.", "stattrak_pulls", 100, 4000, 1 },
    { "stattrak_pulls_250", "Serial Killer", "Pull 250 StatTrak skins from cases.", "stattrak_pulls", 250, 10000, 1 },
    { "gold_first", "Golden Moment", "Pull 1 knife or gloves from cases.", "gold_pulls", 1, 1000, 2 },
    { "gold_pulls_2", "Double Take", "Pull 2 knives or gloves from cases.", "gold_pulls", 2, 300, 2 },
    { "gold_pulls_3", "Triple Threat", "Pull 3 knives or gloves from cases.", "gold_pulls", 3, 500, 2 },
    { "gold_5", "Gold Rush", "Pull 5 knives or gloves from cases.", "gold_pulls", 5, 2500, 2 },
    { "gold_pulls_10", "Gold Digger", "Pull 10 knives or gloves from cases.", "gold_pulls", 10, 1500, 2 },
    { "gold_25", "Midas Touch", "Pull 25 knives or gloves from cases.", "gold_pulls", 25, 8000, 2 },
    { "gold_pulls_50", "Fort Knox", "Pull 50 knives or gloves from cases.", "gold_pulls", 50, 6000, 2 },
    { "gold_pulls_100", "Golden Goose", "Pull 100 knives or gloves from cases.", "gold_pulls", 100, 9750, 2 },
    { "gold_pulls_250", "Dragon's Hoard", "Pull 250 knives or gloves from cases.", "gold_pulls", 250, 15000, 2 },
    { "gold_cases_3", "Gold Trio", "Pull a gold from 3 different cases.", "gold_cases", 3, 150, 2 },
    { "gold_cases_5", "Gold Everywhere", "Pull a gold from 5 different cases.", "gold_cases", 5, 3000, 2 },
    { "gold_cases_10", "Golden Ticket", "Pull a gold from 10 different cases.", "gold_cases", 10, 1500, 2 },
    { "gold_cases_20", "Spread the Wealth", "Pull a gold from 20 different cases.", "gold_cases", 20, 6000, 2 },
    { "gold_cases_30", "Gilded Age", "Pull a gold from 30 different cases.", "gold_cases", 30, 15000, 2 },
    { "gold_streak_best_2", "Back to Back", "Pull golds from 2 cases in a row.", "gold_streak_best", 2, 300, 2 },
    { "gold_streak_best_3", "Hat-Trick", "Pull golds from 3 cases in a row.", "gold_streak_best", 3, 30000, 2 },
    { "dry_streak_best_25", "Cold Streak", "Open 25 cases in a row without a gold.", "dry_streak_best", 25, 150, 2 },
    { "dry_streak_best_50", "Dry Spell", "Open 50 cases in a row without a gold.", "dry_streak_best", 50, 500, 2 },
    { "dry_streak_best_100", "Drought", "Open 100 cases in a row without a gold.", "dry_streak_best", 100, 1400, 2 },
    { "dry_streak_best_250", "Desert Crossing", "Open 250 cases in a row without a gold.", "dry_streak_best", 250, 5600, 2 },
    { "dry_streak_best_500", "Sahara", "Open 500 cases in a row without a gold.", "dry_streak_best", 500, 14000, 2 },
    { "knives_pulled_1", "Sharp Start", "Pull 1 knife from cases.", "knives_pulled", 1, 100, 3 },
    { "knives_pulled_5", "Cutting Edge", "Pull 5 knives from cases.", "knives_pulled", 5, 400, 3 },
    { "knives_pulled_10", "Knife Fight", "Pull 10 knives from cases.", "knives_pulled", 10, 1200, 3 },
    { "knives_pulled_25", "Blade Runner", "Pull 25 knives from cases.", "knives_pulled", 25, 4800, 3 },
    { "knives_pulled_50", "Cutlery Drawer", "Pull 50 knives from cases.", "knives_pulled", 50, 12000, 3 },
    { "gloves_pulled_1", "Hands On", "Pull 1 pair of gloves from cases.", "gloves_pulled", 1, 100, 3 },
    { "gloves_pulled_5", "Glove Story", "Pull 5 pairs of gloves from cases.", "gloves_pulled", 5, 400, 3 },
    { "gloves_pulled_10", "Handy Work", "Pull 10 pairs of gloves from cases.", "gloves_pulled", 10, 1200, 3 },
    { "gloves_pulled_25", "Fingerless", "Pull 25 pairs of gloves from cases.", "gloves_pulled", 25, 4800, 3 },
    { "gloves_pulled_50", "Glove Box", "Pull 50 pairs of gloves from cases.", "gloves_pulled", 50, 12000, 3 },
    { "knife_types_3", "Three Blades", "Pull 3 different types of knife.", "knife_types", 3, 150, 3 },
    { "knife_types_5", "Knife Block", "Pull 5 different types of knife.", "knife_types", 5, 500, 3 },
    { "knife_types_8", "Cutlery Set", "Pull 8 different types of knife.", "knife_types", 8, 1500, 3 },
    { "knife_types_12", "Blade Master", "Pull 12 different types of knife.", "knife_types", 12, 6000, 3 },
    { "knife_types_16", "Armory", "Pull 16 different types of knife.", "knife_types", 16, 15000, 3 },
    { "glove_types_2", "Odd Pair", "Pull 2 different types of gloves.", "glove_types", 2, 150, 3 },
    { "glove_types_3", "Fashion Trio", "Pull 3 different types of gloves.", "glove_types", 3, 500, 3 },
    { "glove_types_4", "Glove Rack", "Pull 4 different types of gloves.", "glove_types", 4, 1500, 3 },
    { "glove_types_5", "Haberdasher", "Pull 5 different types of gloves.", "glove_types", 5, 6000, 3 },
    { "glove_types_7", "Wardrobe", "Pull 7 different types of gloves.", "glove_types", 7, 15000, 3 },
    { "gold_finishes_5", "Gold Sampler", "Pull 5 different knife or glove finishes.", "gold_finishes", 5, 150, 3 },
    { "gold_finishes_10", "Shiny Things", "Pull 10 different knife or glove finishes.", "gold_finishes", 10, 450, 3 },
    { "gold_finishes_25", "Display Case", "Pull 25 different knife or glove finishes.", "gold_finishes", 25, 1300, 3 },
    { "gold_finishes_50", "Gallery", "Pull 50 different knife or glove finishes.", "gold_finishes", 50, 2100, 3 },
    { "gold_finishes_100", "Museum", "Pull 100 different knife or glove finishes.", "gold_finishes", 100, 5200, 3 },
    { "gold_finishes_250", "Treasury", "Pull 250 different knife or glove finishes.", "gold_finishes", 250, 13000, 3 },
    { "skins_found_10", "First Finds", "Find 10 different skins in cases.", "skins_found", 10, 100, 4 },
    { "skins_found_25", "Collector's Start", "Find 25 different skins in cases.", "skins_found", 25, 200, 4 },
    { "skins_found_50", "Binder", "Find 50 different skins in cases.", "skins_found", 50, 600, 4 },
    { "skins_found_100", "Scrapbook", "Find 100 different skins in cases.", "skins_found", 100, 1000, 4 },
    { "skins_found_250", "Archive", "Find 250 different skins in cases.", "skins_found", 250, 1600, 4 },
    { "skins_found_500", "Library", "Find 500 different skins in cases.", "skins_found", 500, 2500, 4 },
    { "skins_found_1000", "Vault", "Find 1,000 different skins in cases.", "skins_found", 1000, 6500, 4 },
    { "skins_found_2000", "Hall of Skins", "Find 2,000 different skins in cases.", "skins_found", 2000, 10000, 4 },
    { "goal_first", "Completionist", "Complete 1 collection (every skin of a case).", "goals_done", 1, 1000, 4 },
    { "goals_done_3", "Three of a Kind", "Complete 3 collections (every skin of a case).", "goals_done", 3, 550, 4 },
    { "goals_5", "Collector", "Complete 5 collections (every skin of a case).", "goals_done", 5, 4000, 4 },
    { "goals_done_10", "Set Builder", "Complete 10 collections (every skin of a case).", "goals_done", 10, 1600, 4 },
    { "goals_15", "Curator", "Complete 15 collections (every skin of a case).", "goals_done", 15, 10000, 4 },
    { "goals_done_25", "Gallery Owner", "Complete 25 collections (every skin of a case).", "goals_done", 25, 10400, 4 },
    { "goals_done_50", "Hall of Fame", "Complete 50 collections (every skin of a case).", "goals_done", 50, 16000, 4 },
    { "tradeup_first", "Contract Killer", "Complete 1 trade-up contract.", "tradeups", 1, 150, 5 },
    { "tradeups_5", "Fine Print", "Complete 5 trade-up contracts.", "tradeups", 5, 350, 5 },
    { "tradeups_10", "Dotted Line", "Complete 10 trade-up contracts.", "tradeups", 10, 600, 5 },
    { "tradeup_25", "Contract Veteran", "Complete 25 trade-up contracts.", "tradeups", 25, 1500, 5 },
    { "tradeups_50", "Broker", "Complete 50 trade-up contracts.", "tradeups", 50, 2500, 5 },
    { "tradeups_100", "Negotiator", "Complete 100 trade-up contracts.", "tradeups", 100, 6500, 5 },
    { "tradeups_250", "Tycoon", "Complete 250 trade-up contracts.", "tradeups", 250, 10000, 5 },
    { "tradeup_gold", "Gold Contract", "Trade up into a knife or gloves 1 time.", "tradeup_golds", 1, 1500, 5 },
    { "tradeup_golds_3", "Alchemist", "Trade up into a knife or gloves 3 times.", "tradeup_golds", 3, 650, 5 },
    { "tradeup_golds_5", "Philosopher's Stone", "Trade up into a knife or gloves 5 times.", "tradeup_golds", 5, 1800, 5 },
    { "tradeup_golds_10", "Transmuter", "Trade up into a knife or gloves 10 times.", "tradeup_golds", 10, 2900, 5 },
    { "tradeup_golds_25", "Master Alchemist", "Trade up into a knife or gloves 25 times.", "tradeup_golds", 25, 7200, 5 },
    { "tradeup_golds_50", "Goldsmith", "Trade up into a knife or gloves 50 times.", "tradeup_golds", 50, 18000, 5 },
    { "kills_1", "First Blood", "Get 1 kill.", "kills", 1, 50, 6 },
    { "kills_10", "Ten Down", "Get 10 kills.", "kills", 10, 100, 6 },
    { "kills_50", "Fifty Fallen", "Get 50 kills.", "kills", 50, 200, 6 },
    { "kills_100", "Centurion", "Get 100 kills.", "kills", 100, 350, 6 },
    { "kills_250", "Marksman", "Get 250 kills.", "kills", 250, 600, 6 },
    { "kills_500", "Hitman", "Get 500 kills.", "kills", 500, 950, 6 },
    { "kills_1000", "Thousand Cuts", "Get 1,000 kills.", "kills", 1000, 1500, 6 },
    { "kills_2500", "Executioner", "Get 2,500 kills.", "kills", 2500, 2400, 6 },
    { "kills_5000", "Reaper", "Get 5,000 kills.", "kills", 5000, 3900, 6 },
    { "kills_10000", "Legend", "Get 10,000 kills.", "kills", 10000, 6000, 6 },
    { "headshots_10", "Head Turner", "Get 10 headshots.", "headshots", 10, 50, 6 },
    { "headshots_50", "Pop Pop", "Get 50 headshots.", "headshots", 50, 250, 6 },
    { "headshots_100", "Skull Cracker", "Get 100 headshots.", "headshots", 100, 400, 6 },
    { "headshots_250", "Dome Delivery", "Get 250 headshots.", "headshots", 250, 700, 6 },
    { "headshots_500", "Headhunter", "Get 500 headshots.", "headshots", 500, 1750, 6 },
    { "headshots_1000", "Nothing But Dome", "Get 1,000 headshots.", "headshots", 1000, 4550, 6 },
    { "headshots_2500", "Brain Surgeon", "Get 2,500 headshots.", "headshots", 2500, 7000, 6 },
    { "mvps_1", "Most Valuable", "Be the MVP of 1 round.", "mvps", 1, 100, 6 },
    { "mvps_5", "Fan Favorite", "Be the MVP of 5 rounds.", "mvps", 5, 300, 6 },
    { "mvps_10", "Star Player", "Be the MVP of 10 rounds.", "mvps", 10, 500, 6 },
    { "mvps_25", "MVP Streak", "Be the MVP of 25 rounds.", "mvps", 25, 800, 6 },
    { "mvps_50", "Showstopper", "Be the MVP of 50 rounds.", "mvps", 50, 2000, 6 },
    { "mvps_100", "Franchise Player", "Be the MVP of 100 rounds.", "mvps", 100, 5200, 6 },
    { "mvps_250", "Franchise Legend", "Be the MVP of 250 rounds.", "mvps", 250, 8000, 6 },
    { "plants_1", "Bomb Voyage", "Plant the bomb 1 time.", "plants", 1, 50, 6 },
    { "plants_10", "Demolition", "Plant the bomb 10 times.", "plants", 10, 250, 6 },
    { "plants_25", "Pyro", "Plant the bomb 25 times.", "plants", 25, 700, 6 },
    { "plants_50", "Fuse Lighter", "Plant the bomb 50 times.", "plants", 50, 2800, 6 },
    { "plants_100", "Tick Tock", "Plant the bomb 100 times.", "plants", 100, 7000, 6 },
    { "defuses_1", "Snip Snip", "Defuse the bomb 1 time.", "defuses", 1, 100, 6 },
    { "defuses_10", "Cool Hands", "Defuse the bomb 10 times.", "defuses", 10, 300, 6 },
    { "defuses_25", "Wire Cutter", "Defuse the bomb 25 times.", "defuses", 25, 800, 6 },
    { "defuses_50", "Bomb Squad", "Defuse the bomb 50 times.", "defuses", 50, 3200, 6 },
    { "defuses_100", "Last Second", "Defuse the bomb 100 times.", "defuses", 100, 8000, 6 },
    { "matches_1", "Newcomer", "Finish 1 match.", "matches", 1, 100, 6 },
    { "matches_5", "Getting the Hang", "Finish 5 matches.", "matches", 5, 150, 6 },
    { "matches_10", "Veteran Season", "Finish 10 matches.", "matches", 10, 500, 6 },
    { "matches_25", "Grinder", "Finish 25 matches.", "matches", 25, 800, 6 },
    { "matches_50", "Fifty Games", "Finish 50 matches.", "matches", 50, 1300, 6 },
    { "matches_100", "Century", "Finish 100 matches.", "matches", 100, 2000, 6 },
    { "matches_250", "Lifer", "Finish 250 matches.", "matches", 250, 5200, 6 },
    { "matches_500", "Immortal", "Finish 500 matches.", "matches", 500, 8000, 6 },
    { "stars_earned_1000", "Pocket Change", "Earn 1,000 stars in matches.", "stars_earned", 1000, 100, 7 },
    { "stars_10k", "Star Saver", "Earn 10,000 stars in matches.", "stars_earned", 10000, 500, 7 },
    { "stars_earned_50000", "Piggy Bank", "Earn 50,000 stars in matches.", "stars_earned", 50000, 500, 7 },
    { "stars_100k", "Starstruck", "Earn 100,000 stars in matches.", "stars_earned", 100000, 3000, 7 },
    { "stars_earned_250000", "Star Hoarder", "Earn 250,000 stars in matches.", "stars_earned", 250000, 2000, 7 },
    { "stars_earned_500000", "Constellation", "Earn 500,000 stars in matches.", "stars_earned", 500000, 5200, 7 },
    { "stars_earned_1000000", "Supernova", "Earn 1,000,000 stars in matches.", "stars_earned", 1000000, 8000, 7 },
};

std::string FormatCaseName(const ItemSchema &schema, uint32_t crate)
{
    if (const std::string *custom = schema.CustomCaseName(crate))
    {
        return *custom;
    }

    const ItemInfo *info = schema.ItemInfoByDefIndex(crate);
    std::string name = info ? info->m_name : "case " + std::to_string(crate);
    if (name.compare(0, 6, "crate_") == 0)
    {
        name.erase(0, 6);
    }
    for (char &c : name)
    {
        if (c == '_')
        {
            c = ' ';
        }
    }
    return name;
}

void FlattenLootList(const LootList &list, bool gold, Career::CaseInfo &info, std::set<Career::Skin> &seen,
    std::unordered_set<const LootList *> &visited, bool &hasNonSkin)
{
    if (!visited.insert(&list).second)
    {
        return;
    }

    for (const LootListItem &item : list.items)
    {
        if (gold || list.isUnusual || item.CaseRarity() == ItemSchema::RarityUnusual)
        {
            info.goldItems++;
            continue;
        }

        if (!item.itemInfo)
        {
            continue;
        }

        if (!item.paintKitInfo)
        {
            hasNonSkin = true;
            continue;
        }

        const Career::Skin skin{ item.itemInfo->m_defIndex, item.paintKitInfo ? item.paintKitInfo->m_defIndex : 0 };
        if (seen.insert(skin).second)
        {
            info.skins.push_back(skin);
        }
    }

    for (const LootList *sub : list.subLists)
    {
        if (sub)
        {
            FlattenLootList(*sub, gold || list.isUnusual, info, seen, visited, hasNonSkin);
        }
    }
}

std::mutex s_publishMutex;
std::string s_publishedData;
bool s_publishedDirty;

} // namespace

void Career::Publish(std::string uiData)
{
    std::lock_guard lock{ s_publishMutex };
    s_publishedData = std::move(uiData);
    s_publishedDirty = true;
}

bool Career::TakePublished(std::string &uiData)
{
    std::lock_guard lock{ s_publishMutex };
    if (!s_publishedDirty)
    {
        return false;
    }

    s_publishedDirty = false;
    uiData = s_publishedData;
    return !uiData.empty();
}

void Career::RepublishLast()
{
    std::lock_guard lock{ s_publishMutex };
    s_publishedDirty = !s_publishedData.empty();
}

Career::Career(std::string path)
    : m_path{ std::move(path) }
{
}

uint64_t Career::Stat(const std::string &name) const
{
    auto it = m_stats.find(name);
    return it != m_stats.end() ? it->second : 0;
}

// lines: "stat <name> <n>", "case <crate> <opened> <golds> <complete>",
// "found <crate> <defIndex> <paintKit>", "ach <id> <time>"
void Career::Load()
{
    m_stats.clear();
    m_cases.clear();
    m_achievements.clear();
    m_goldFinishes.clear();
    m_knifeTypes.clear();
    m_gloveTypes.clear();

    std::ifstream file{ m_path };
    std::string line;
    while (std::getline(file, line))
    {
        std::istringstream in{ line };
        std::string kind;
        in >> kind;
        if (kind == "stat")
        {
            std::string name;
            uint64_t value;
            if (in >> name >> value)
            {
                m_stats[name] = value;
            }
        }
        else if (kind == "case")
        {
            uint32_t crate, opened, golds, complete;
            if (in >> crate >> opened >> golds >> complete)
            {
                CaseProgress &progress = m_cases[crate];
                progress.opened = opened;
                progress.golds = golds;
                progress.complete = complete != 0;
            }
        }
        else if (kind == "found")
        {
            uint32_t crate, defIndex, paintKit;
            if (in >> crate >> defIndex >> paintKit)
            {
                m_cases[crate].found.insert({ defIndex, paintKit });
            }
        }
        else if (kind == "goldfinish")
        {
            uint32_t defIndex, paintKit;
            if (in >> defIndex >> paintKit)
            {
                m_goldFinishes.insert({ defIndex, paintKit });
            }
        }
        else if (kind == "knifetype" || kind == "glovetype")
        {
            uint32_t defIndex;
            if (in >> defIndex)
            {
                (kind == "knifetype" ? m_knifeTypes : m_gloveTypes).insert(defIndex);
            }
        }
        else if (kind == "ach")
        {
            std::string id;
            uint32_t when;
            if (in >> id >> when)
            {
                m_achievements[id] = when;
            }
        }
    }
}

bool Career::Save() const
{
    std::string tempPath = m_path + ".tmp";
    {
        std::ofstream file{ tempPath, std::ios::trunc };
        if (!file)
        {
            return false;
        }

        for (const auto &[name, value] : m_stats)
        {
            file << "stat " << name << ' ' << value << '\n';
        }
        for (const auto &[crate, progress] : m_cases)
        {
            file << "case " << crate << ' ' << progress.opened << ' ' << progress.golds << ' '
                 << (progress.complete ? 1 : 0) << '\n';
            for (const Skin &skin : progress.found)
            {
                file << "found " << crate << ' ' << skin.defIndex << ' ' << skin.paintKit << '\n';
            }
        }
        for (const Skin &skin : m_goldFinishes)
        {
            file << "goldfinish " << skin.defIndex << ' ' << skin.paintKit << '\n';
        }
        for (uint32_t defIndex : m_knifeTypes)
        {
            file << "knifetype " << defIndex << '\n';
        }
        for (uint32_t defIndex : m_gloveTypes)
        {
            file << "glovetype " << defIndex << '\n';
        }
        for (const auto &[id, when] : m_achievements)
        {
            file << "ach " << id << ' ' << when << '\n';
        }

        if (!file)
        {
            return false;
        }
    }

    remove(m_path.c_str());
    return rename(tempPath.c_str(), m_path.c_str()) == 0;
}

std::vector<Career::Unlock> Career::CheckAchievements()
{
    std::vector<Unlock> unlocked;
    for (const AchievementDef &def : Achievements)
    {
        if (m_achievements.contains(def.id) || Stat(def.stat) < def.threshold)
        {
            continue;
        }

        m_achievements[def.id] = static_cast<uint32_t>(time(nullptr));
        unlocked.push_back({ def.title, std::string{ "Achievement unlocked: " } + def.title, def.stars });
    }
    return unlocked;
}

std::vector<Career::Unlock> Career::RecordCaseOpened(uint32_t crate, const CaseInfo &info, const Pull &pull)
{
    std::vector<Unlock> unlocked;

    // sticker capsules and the like have no skins to collect
    if (info.skins.empty())
    {
        m_stats["other_opened"]++;
        unlocked = CheckAchievements();
        Save();
        return unlocked;
    }

    m_stats["cases_opened"]++;
    if (info.custom)
    {
        m_stats["custom_cases_opened"]++;
    }

    CaseProgress &progress = m_cases[crate];
    progress.opened++;

    if (pull.gold)
    {
        m_stats["gold_pulls"]++;
        if (!progress.golds)
        {
            m_stats["gold_cases"]++;
        }
        progress.golds++;

        m_stats[pull.glove ? "gloves_pulled" : "knives_pulled"]++;
        (pull.glove ? m_gloveTypes : m_knifeTypes).insert(pull.skin.defIndex);
        m_goldFinishes.insert(pull.skin);
        m_stats["knife_types"] = m_knifeTypes.size();
        m_stats["glove_types"] = m_gloveTypes.size();
        m_stats["gold_finishes"] = m_goldFinishes.size();
    }
    else
    {
        switch (pull.rarity)
        {
        case ItemSchema::RarityAncient: m_stats["pulls_covert"]++; break;
        case ItemSchema::RarityLegendary: m_stats["pulls_classified"]++; break;
        case ItemSchema::RarityMythical: m_stats["pulls_restricted"]++; break;
        default: m_stats["pulls_milspec"]++; break;
        }
    }

    // streaks: golds in a row, and cases in a row without one
    if (pull.gold)
    {
        const uint64_t streak = ++m_stats["gold_streak"];
        m_stats["gold_streak_best"] = std::max(m_stats["gold_streak_best"], streak);
        m_stats["dry_streak"] = 0;
    }
    else
    {
        m_stats["gold_streak"] = 0;
        const uint64_t dry = ++m_stats["dry_streak"];
        m_stats["dry_streak_best"] = std::max(m_stats["dry_streak_best"], dry);
    }

    if (pull.statTrak)
    {
        m_stats["stattrak_pulls"]++;
    }

    // a regular skin counts toward the case's collection goal
    if (!pull.gold)
    {
        progress.found.insert(pull.skin);
    }

    uint64_t distinct = 0, found = 0;
    for (const auto &entry : m_cases)
    {
        distinct += entry.second.opened ? 1 : 0;
        found += entry.second.found.size();
    }
    m_stats["cases_distinct"] = distinct;
    m_stats["skins_found"] = found;

    if (!progress.complete)
    {
        bool all = true;
        for (const Skin &skin : info.skins)
        {
            if (!progress.found.contains(skin))
            {
                all = false;
                break;
            }
        }

        if (all)
        {
            progress.complete = true;
            m_stats["goals_done"]++;
            unlocked.push_back({ info.name, "Collection complete: " + info.name
                    + " (every skin found)", GoalReward });
        }
    }

    std::vector<Unlock> achievements = CheckAchievements();
    unlocked.insert(unlocked.end(), achievements.begin(), achievements.end());

    Save();
    return unlocked;
}

std::vector<Career::Unlock> Career::RecordMatchEvent(MatchEvent event)
{
    static const char *const Names[] = { "kills", "headshots", "mvps", "plants", "defuses", "matches" };
    m_stats[Names[static_cast<size_t>(event)]]++;

    std::vector<Unlock> unlocked = CheckAchievements();
    // these come in one at a time during a match; save at the end of one or on an unlock
    if (!unlocked.empty() || event == MatchEvent::MatchComplete || ++m_unsavedStarEvents >= 50)
    {
        m_unsavedStarEvents = 0;
        Save();
    }
    return unlocked;
}

std::vector<Career::Unlock> Career::RecordTradeUp(bool goldResult)
{
    m_stats["tradeups"]++;
    if (goldResult)
    {
        m_stats["tradeup_golds"]++;
    }

    std::vector<Unlock> unlocked = CheckAchievements();
    Save();
    return unlocked;
}

std::vector<Career::Unlock> Career::RecordStarsEarned(uint32_t amount)
{
    m_stats["stars_earned"] += amount;

    std::vector<Unlock> unlocked = CheckAchievements();
    // stars come in one at a time during a match; don't write the file for each
    if (!unlocked.empty() || ++m_unsavedStarEvents >= 50)
    {
        m_unsavedStarEvents = 0;
        Save();
    }
    return unlocked;
}

std::string Career::StatsText() const
{
    std::ostringstream out;
    out << "Cases opened: " << Stat("cases_opened") << " (" << Stat("custom_cases_opened") << " custom, "
        << Stat("cases_distinct") << " different)\n"
        << "Pulls: " << Stat("pulls_milspec") << " Mil-Spec, " << Stat("pulls_restricted") << " Restricted, "
        << Stat("pulls_classified") << " Classified, " << Stat("pulls_covert") << " Covert\n"
        << "Golds: " << Stat("gold_pulls") << " (" << Stat("knives_pulled") << " knives, " << Stat("gloves_pulled")
        << " gloves, from " << Stat("gold_cases") << " different cases), " << Stat("gold_finishes")
        << " different finishes\n"
        << "Cases since your last gold: " << Stat("dry_streak") << " (longest run " << Stat("dry_streak_best") << ")\n"
        << "StatTrak pulls: " << Stat("stattrak_pulls") << '\n'
        << "Skins found: " << Stat("skins_found") << ", collections complete: " << Stat("goals_done") << '\n'
        << "Trade-ups: " << Stat("tradeups") << " (" << Stat("tradeup_golds") << " gold)\n"
        << "Matches: " << Stat("matches") << ", kills " << Stat("kills") << " (" << Stat("headshots")
        << " headshots), MVPs " << Stat("mvps") << ", plants " << Stat("plants") << ", defuses " << Stat("defuses") << '\n'
        << "Stars earned in matches: " << Stat("stars_earned") << '\n'
        << "Achievements: " << m_achievements.size() << " of " << std::size(Achievements);
    return out.str();
}

std::string Career::GoalsText(const std::vector<std::pair<uint32_t, CaseInfo>> &cases, bool showAll) const
{
    std::ostringstream out;
    size_t shown = 0;
    for (const auto &[crate, info] : cases)
    {
        auto it = m_cases.find(crate);
        const bool opened = it != m_cases.end() && it->second.opened;
        if (info.skins.empty() || (!opened && !showAll))
        {
            continue;
        }

        size_t found = 0;
        if (it != m_cases.end())
        {
            for (const Skin &skin : info.skins)
            {
                found += it->second.found.contains(skin) ? 1 : 0;
            }
        }

        out << info.name << ": " << found << "/" << info.skins.size() << " skins";
        if (info.goldItems)
        {
            out << (it != m_cases.end() && it->second.golds ? ", gold found" : ", gold not found");
        }
        if (it != m_cases.end() && it->second.complete)
        {
            out << "  [COMPLETE]";
        }
        out << '\n';
        shown++;
    }

    if (!shown)
    {
        return showAll ? "no cases" : "no cases opened yet (goals all lists every case)";
    }

    std::string text = out.str();
    text.pop_back();
    return text;
}

static std::string JsString(const std::string &text)
{
    std::string out = "'";
    for (char c : text)
    {
        if (c == '\\' || c == '\'')
        {
            out += '\\';
        }

        if (static_cast<unsigned char>(c) < 0x20)
        {
            out += ' ';
        }
        else
        {
            out += c;
        }
    }
    return out + "'";
}

// a JS object for the SkinStop tabs:
//   stats [label, value], categories [name], achievements [id, title, desc, stars, threshold, progress, done, category],
//   cases [crate, name, found, total, golds, goldFound, complete, opened]
std::string Career::UiData(const std::vector<std::pair<uint32_t, CaseInfo>> &cases) const
{
    std::ostringstream out;
    out << "{ goalReward: " << GoalReward << ", stats: [";
    const std::pair<const char *, std::string> stats[] = {
        { "Cases opened", std::to_string(Stat("cases_opened")) },
        { "Custom cases opened", std::to_string(Stat("custom_cases_opened")) },
        { "Different cases opened", std::to_string(Stat("cases_distinct")) },
        { "Skins found for collections", std::to_string(Stat("skins_found")) },
        { "Collections complete", std::to_string(Stat("goals_done")) },
        { "Mil-Spec skins pulled", std::to_string(Stat("pulls_milspec")) },
        { "Restricted skins pulled", std::to_string(Stat("pulls_restricted")) },
        { "Classified skins pulled", std::to_string(Stat("pulls_classified")) },
        { "Covert skins pulled", std::to_string(Stat("pulls_covert")) },
        { "StatTrak skins pulled", std::to_string(Stat("stattrak_pulls")) },
        { "Knives pulled", std::to_string(Stat("knives_pulled")) },
        { "Gloves pulled", std::to_string(Stat("gloves_pulled")) },
        { "Cases that gave a gold", std::to_string(Stat("gold_cases")) },
        { "Different knife types", std::to_string(Stat("knife_types")) },
        { "Different glove types", std::to_string(Stat("glove_types")) },
        { "Different gold finishes", std::to_string(Stat("gold_finishes")) },
        { "Best run of golds", std::to_string(Stat("gold_streak_best")) },
        { "Cases since your last gold", std::to_string(Stat("dry_streak")) },
        { "Longest dry run", std::to_string(Stat("dry_streak_best")) },
        { "Trade-up contracts", std::to_string(Stat("tradeups")) },
        { "Gold trade-ups", std::to_string(Stat("tradeup_golds")) },
        { "Kills", std::to_string(Stat("kills")) },
        { "Headshots", std::to_string(Stat("headshots")) },
        { "Round MVPs", std::to_string(Stat("mvps")) },
        { "Bombs planted", std::to_string(Stat("plants")) },
        { "Bombs defused", std::to_string(Stat("defuses")) },
        { "Matches finished", std::to_string(Stat("matches")) },
        { "Stars earned in matches", std::to_string(Stat("stars_earned")) },
    };
    bool first = true;
    for (const auto &stat : stats)
    {
        out << (first ? "" : ",") << "[" << JsString(stat.first) << "," << JsString(stat.second) << "]";
        first = false;
    }

    out << "], categories: [";
    first = true;
    for (const char *category : AchievementCategories)
    {
        out << (first ? "" : ",") << JsString(category);
        first = false;
    }

    out << "], achievements: [";
    first = true;
    for (const AchievementDef &def : Achievements)
    {
        out << (first ? "" : ",") << "[" << JsString(def.id) << "," << JsString(def.title) << "," << JsString(def.desc)
            << "," << def.stars << "," << def.threshold << "," << std::min(Stat(def.stat), def.threshold) << ","
            << (m_achievements.contains(def.id) ? 1 : 0) << "," << def.category << "]";
        first = false;
    }

    out << "], cases: [";
    first = true;
    for (const auto &[crate, info] : cases)
    {
        if (info.skins.empty())
        {
            continue;
        }

        auto it = m_cases.find(crate);
        size_t found = 0;
        if (it != m_cases.end())
        {
            for (const Skin &skin : info.skins)
            {
                found += it->second.found.contains(skin) ? 1 : 0;
            }
        }

        out << (first ? "" : ",") << "[" << crate << "," << JsString(info.name) << "," << found << ","
            << info.skins.size() << "," << info.goldItems << ","
            << (it != m_cases.end() && it->second.golds ? 1 : 0) << ","
            << (it != m_cases.end() && it->second.complete ? 1 : 0) << ","
            << (it != m_cases.end() ? it->second.opened : 0) << "]";
        first = false;
    }
    out << "] }";
    return out.str();
}

std::string Career::AchievementsText() const
{
    std::ostringstream out;
    for (const AchievementDef &def : Achievements)
    {
        const bool done = m_achievements.contains(def.id);
        out << (done ? "[x] " : "[ ] ") << def.title << " - " << def.desc << " (+" << def.stars << " stars)";
        if (!done)
        {
            out << "  " << std::min(Stat(def.stat), def.threshold) << "/" << def.threshold;
        }
        out << '\n';
    }

    std::string text = out.str();
    text.pop_back();
    return text;
}

bool Career::BuildCaseInfo(const ItemSchema &schema, uint32_t crate, CaseInfo &info)
{
    const LootList *list = schema.GetCrateLootList(crate);
    if (!list)
    {
        return false;
    }

    info = {};
    info.name = FormatCaseName(schema, crate);
    info.custom = schema.IsCustomCase(crate);

    std::set<Skin> seen;
    std::unordered_set<const LootList *> visited;
    bool hasNonSkin = false;
    FlattenLootList(*list, false, info, seen, visited, hasNonSkin);
    // a sticker capsule, music kit box or the like: nothing to collect
    if (hasNonSkin)
    {
        info.skins.clear();
    }
    return true;
}

bool Career::BuildPull(const ItemSchema &schema, const CSOEconItem &item, Pull &pull)
{
    pull = {};
    pull.skin.defIndex = item.def_index();
    uint32_t paintKit = 0;
    if (GetItemPaintKitDefIndex(item, schema, paintKit))
    {
        pull.skin.paintKit = paintKit;
    }

    pull.rarity = item.rarity();
    pull.gold = item.quality() == ItemSchema::QualityUnusual;
    if (const ItemInfo *info = schema.ItemInfoByDefIndex(item.def_index()))
    {
        pull.glove = info->m_itemType == "gloves" || info->m_name.find("glove") != std::string::npos;
    }
    pull.statTrak = item.quality() == ItemSchema::QualityStrange;
    return true;
}
