#pragma once

class ItemSchema;
class Random;

struct LootListItem;
struct LootList;

class SouvenirOpening
{
public:
    SouvenirOpening(const ItemSchema &itemSchema, Random &random);

    bool OpenPackage(const CSOEconItem &package, CSOEconItem &item);

private:
    const LootListItem *SelectItem(const std::vector<const LootListItem *> &items);
    void ApplyTournamentAttributes(CSOEconItem &item, uint32_t eventId, uint32_t teamId1, uint32_t teamId2, uint32_t mvpAccountId);

    const ItemSchema &m_itemSchema;
    Random &m_random;
};
