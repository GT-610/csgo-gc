#pragma once

class ItemSchema;
class Random;

struct LootListItem;
struct LootList;

class CaseOpening
{
public:
    CaseOpening(const ItemSchema &itemSchema, Random &random);

    struct PityResult
    {
        bool counted{}; // the crate can contain a gold, so it counts toward pity
        bool gotGold{};
        float baseChance{}; // the crate's gold chance without pity
        float goldChance{}; // the gold chance this roll was made with
    };

    // goldlessCases: cases opened since the last gold, used for the pity system
    bool SelectItemFromCrate(const CSOEconItem &crate, CSOEconItem &item,
        uint32_t goldlessCases = 0, PityResult *pityResult = nullptr);

    // gold chance for the next case given the base chance and the pity counter
    static float PityGoldChance(float baseChance, uint32_t goldlessCases);

    // weight multiplier for a non-gold rarity while waiting for a gold
    static float PityRarityBoost(uint32_t rarity, uint32_t goldlessCases);

private:
    const LootListItem *SelectLootListItem(const std::vector<const LootListItem *> &items,
        uint32_t goldlessCases, PityResult &pity);
    uint32_t RandomRarityForItems(const std::vector<const LootListItem *> &items,
        uint32_t goldlessCases, PityResult &pity);
    bool ShouldMakeStatTrak(const LootListItem &item, const LootList &lootList, bool containsUnusuals);

    const ItemSchema &m_itemSchema;
    Random &m_random;
};
