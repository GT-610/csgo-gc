#include "stdafx.h"
#include "career.h"
#include "item_schema.h"
#include "item_utils.h"
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace
{

struct AchievementDef
{
    const char *id;
    const char *title;
    const char *stat;     // the counter it watches
    uint64_t threshold;
    uint32_t stars;
};

// one table: a counter reaching a number. "gold_cases" and "goals_done" are
// kept up to date by the career itself
constexpr AchievementDef Achievements[] = {
    { "first_case", "First Crack", "cases_opened", 1, 100 },
    { "cases_10", "Warming Up", "cases_opened", 10, 250 },
    { "cases_100", "Case Hardened", "cases_opened", 100, 1000 },
    { "cases_500", "Unboxing Addict", "cases_opened", 500, 3000 },
    { "cases_1000", "Thousand Keys", "cases_opened", 1000, 6000 },
    { "custom_10", "House Brand", "custom_cases_opened", 10, 500 },
    { "custom_100", "Loyal Customer", "custom_cases_opened", 100, 2500 },
    { "covert_first", "Red Alert", "pulls_covert", 1, 300 },
    { "covert_25", "Seeing Red", "pulls_covert", 25, 1500 },
    { "gold_first", "Golden Moment", "gold_pulls", 1, 1000 },
    { "gold_5", "Gold Rush", "gold_pulls", 5, 2500 },
    { "gold_25", "Midas Touch", "gold_pulls", 25, 8000 },
    { "gold_cases_5", "Gold Everywhere", "gold_cases", 5, 3000 },
    { "stattrak_first", "Counting Kills", "stattrak_pulls", 1, 200 },
    { "tradeup_first", "Contract Killer", "tradeups", 1, 150 },
    { "tradeup_25", "Contract Veteran", "tradeups", 25, 1500 },
    { "tradeup_gold", "Gold Contract", "tradeup_golds", 1, 1500 },
    { "goal_first", "Completionist", "goals_done", 1, 1000 },
    { "goals_5", "Collector", "goals_done", 5, 4000 },
    { "goals_15", "Curator", "goals_done", 15, 10000 },
    { "stars_10k", "Star Saver", "stars_earned", 10000, 500 },
    { "stars_100k", "Starstruck", "stars_earned", 100000, 3000 },
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

void FlattenLootList(const LootList &list, bool gold, Career::CaseInfo &info,
    std::set<Career::Skin> &seen, std::unordered_set<const LootList *> &visited)
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
            FlattenLootList(*sub, gold || list.isUnusual, info, seen, visited);
        }
    }
}

} // namespace

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
    }
    else if (pull.rarity == ItemSchema::RarityAncient)
    {
        m_stats["pulls_covert"]++;
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

    if (!progress.complete && !info.skins.empty())
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
    out << "Cases opened: " << Stat("cases_opened") << " (" << Stat("custom_cases_opened") << " custom)\n"
        << "Covert pulls: " << Stat("pulls_covert") << ", golds: " << Stat("gold_pulls")
        << " (from " << Stat("gold_cases") << " different cases)\n"
        << "StatTrak pulls: " << Stat("stattrak_pulls") << '\n'
        << "Trade-ups: " << Stat("tradeups") << " (" << Stat("tradeup_golds") << " gold)\n"
        << "Collections complete: " << Stat("goals_done") << '\n'
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

std::string Career::AchievementsText() const
{
    std::ostringstream out;
    for (const AchievementDef &def : Achievements)
    {
        const bool done = m_achievements.contains(def.id);
        out << (done ? "[x] " : "[ ] ") << def.title << " - " << def.stat << " " << def.threshold
            << " (+" << def.stars << " stars)";
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
    FlattenLootList(*list, false, info, seen, visited);
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
    pull.statTrak = item.quality() == ItemSchema::QualityStrange;
    return true;
}
