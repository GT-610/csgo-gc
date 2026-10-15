#include "stdafx.h"
#include "case_opening.h"
#include "config.h"
#include "item_schema.h"
#include "random.h"

CaseOpening::CaseOpening(const ItemSchema &itemSchema, Random &random)
    : m_itemSchema{ itemSchema }
    , m_random{ random }
{
}

// returns true if there are unusuals (stattrak able)
static bool GetLootListItems(const LootList &lootList, std::vector<const LootListItem *> &items)
{
    bool unusuals = lootList.isUnusual;

    for (const LootList *other : lootList.subLists)
    {
        unusuals |= GetLootListItems(*other, items);
    }

    for (const LootListItem &item : lootList.items)
    {
        items.push_back(&item);
    }

    return unusuals;
}

static bool CompareRarity(const LootListItem *a, const LootListItem *b) { return a->CaseRarity() < b->CaseRarity(); }
static bool RarityLower(const LootListItem *a, uint32_t b) { return a->CaseRarity() < b; }
static bool RarityUpper(uint32_t a, const LootListItem *b) { return a < b->CaseRarity(); }

bool CaseOpening::SelectItemFromCrate(const CSOEconItem &crate, CSOEconItem &item,
    uint32_t goldlessCases, PityResult *pityResult)
{
    PityResult pity;

    const LootList *lootList = m_itemSchema.GetCrateLootList(crate.def_index());
    if (!lootList)
    {
        assert(false);
        return false;
    }

    assert(lootList->subLists.empty() != lootList->items.empty());

    std::vector<const LootListItem *> lootListItems;
    lootListItems.reserve(32); // overkill
    bool containsUnusuals = GetLootListItems(*lootList, lootListItems);
    if (!lootListItems.size())
    {
        assert(false);
        return false;
    }

    // must be sorted for SelectLootListItem and friends
    std::sort(lootListItems.begin(), lootListItems.end(), CompareRarity);

    const LootListItem *lootListItem = SelectLootListItem(lootListItems, goldlessCases, pity);
    if (!lootListItem)
    {
        assert(false);
        return false;
    }

    if (pityResult)
    {
        *pityResult = pity;
    }

    bool statTrak = ShouldMakeStatTrak(*lootListItem, *lootList, containsUnusuals);
    return m_itemSchema.CreateItemFromLootListItem(m_random, *lootListItem, statTrak, ItemOriginCrate, UnacknowledgedFoundInCrate, item);
}

// get a range of loot list items with a specific rarity from a vector sorted by rarity
static std::pair<size_t, size_t> FindRarityRange(const std::vector<const LootListItem *> &items, uint32_t rarity)
{
    // MUST have items and they MUST be sorted
    assert(items.size() && std::is_sorted(items.begin(), items.end(), CompareRarity));

    auto lower = std::lower_bound(items.begin(), items.end(), rarity, RarityLower);
    auto upper = std::upper_bound(lower, items.end(), rarity, RarityUpper);

    size_t begin = std::distance(items.begin(), lower);
    size_t end = std::distance(items.begin(), upper);

    return std::make_pair(begin, end);
}

const LootListItem *CaseOpening::SelectLootListItem(const std::vector<const LootListItem *> &items,
    uint32_t goldlessCases, PityResult &pity)
{
    uint32_t rarity = RandomRarityForItems(items, goldlessCases, pity);

    auto [begin, end] = FindRarityRange(items, rarity);
    if (begin == end)
    {
        assert(false);
        return nullptr;
    }

    size_t index = m_random.Integer(begin, end - 1);
    return items[index];
}

float CaseOpening::PityGoldChance(float baseChance, uint32_t goldlessCases)
{
    const CasePityConfig &config = GetConfig().CasePity();
    if (!config.enabled)
    {
        return baseChance;
    }

    // this is case number goldlessCases + 1 since the last gold
    const uint32_t caseNumber = goldlessCases + 1;
    if (caseNumber >= config.guaranteedAt)
    {
        return 1.0f;
    }

    if (caseNumber <= config.softStart)
    {
        return baseChance;
    }

    // ease in from the base chance toward 100% at the guaranteed case
    float t = static_cast<float>(caseNumber - config.softStart)
        / static_cast<float>(config.guaranteedAt - config.softStart);
    return baseChance + (1.0f - baseChance) * t * t;
}

float CaseOpening::PityRarityBoost(uint32_t rarity, uint32_t goldlessCases)
{
    const CasePityConfig &config = GetConfig().CasePity();
    if (!config.enabled)
    {
        return 1.0f;
    }

    for (const RarityWeight &boost : config.rarityBoost)
    {
        if (boost.rarity == rarity)
        {
            // climbs steadily: no boost right after a gold, the full boost
            // by the guaranteed case
            const float t = std::min(1.0f,
                static_cast<float>(goldlessCases) / static_cast<float>(config.guaranteedAt));
            return 1.0f + (boost.weight - 1.0f) * t;
        }
    }

    return 1.0f;
}

uint32_t CaseOpening::RandomRarityForItems(const std::vector<const LootListItem *> &items,
    uint32_t goldlessCases, PityResult &pity)
{
    // MUST have items and they MUST be sorted
    assert(items.size() && std::is_sorted(items.begin(), items.end(), CompareRarity));

    std::vector<RarityWeight> weights;
    weights.reserve(items.size()); // overkill

    float totalWeight = 0;
    float goldWeight = 0;

    // items are sorted by rarity, so iterate through the available rarities like this
    for (size_t i = 0; i < items.size(); i++)
    {
        uint32_t rarity = items[i]->CaseRarity();
        float weight = GetConfig().GetRarityWeight(rarity);

        if (rarity == ItemSchema::RarityUnusual)
        {
            goldWeight = weight;
        }
        else
        {
            weights.push_back({ rarity, weight });
            totalWeight += weight;
        }

        // skip over any duplicate rarities
        while (i + 1 < items.size() && items[i]->CaseRarity() == items[i + 1]->CaseRarity())
        {
            i++;
        }
    }

    // golds are rolled first so the pity system can raise their chance. With no
    // pity bonus this gives exactly the same odds as one weighted roll.
    const bool hasGold = goldWeight > 0.0f;
    if (hasGold)
    {
        pity.counted = true;
        pity.baseChance = goldWeight / (goldWeight + totalWeight);
        pity.goldChance = PityGoldChance(pity.baseChance, goldlessCases);
        if (m_random.Float(0.0f, 1.0f) < pity.goldChance || weights.empty())
        {
            pity.gotGold = true;
            return ItemSchema::RarityUnusual;
        }
    }

    // no gold this time: the longer the gold-less streak, the better the
    // other rarities (only for cases that can contain a gold)
    if (hasGold)
    {
        totalWeight = 0.0f;
        for (RarityWeight &pair : weights)
        {
            pair.weight *= PityRarityBoost(pair.rarity, goldlessCases);
            totalWeight += pair.weight;
        }
    }

    // play the game of chance...
    float value = m_random.Float(0.0f, totalWeight);
    float accum = 0.0f;

    for (const RarityWeight &pair : weights)
    {
        accum += pair.weight;
        if (value < accum)
        {
            return pair.rarity;
        }
    }

    // float rounding can land exactly on totalWeight
    if (!weights.empty())
    {
        return weights.back().rarity;
    }

    assert(false);
    return 0;
}

bool CaseOpening::ShouldMakeStatTrak(const LootListItem &item, const LootList &lootList, bool containsUnusuals)
{
    if (lootList.willProduceStatTrak)
    {
        // must be stattrak
        return true;
    }

    if (!containsUnusuals)
    {
        // not a chance
        return false;
    }

    // unusual stattraks only valid below id 1000
    if (item.quality == ItemSchema::QualityUnusual
        && item.itemInfo->m_defIndex >= 1000)
    {
        return false;
    }

    // roll the dice
    return (m_random.Integer(1, 10) == 1);
}
