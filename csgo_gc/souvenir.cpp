#include "stdafx.h"
#include "souvenir.h"
#include "config.h"
#include "item_schema.h"
#include "random.h"

SouvenirOpening::SouvenirOpening(const ItemSchema &itemSchema, Random &random)
    : m_itemSchema{ itemSchema }
    , m_random{ random }
{
}

static void GetLootListItems(const LootList &lootList, std::vector<const LootListItem *> &items)
{
    for (const LootList *subList : lootList.subLists)
    {
        GetLootListItems(*subList, items);
    }

    for (const LootListItem &item : lootList.items)
    {
        items.push_back(&item);
    }
}

static bool CompareRarity(const LootListItem *a, const LootListItem *b) { return a->CaseRarity() < b->CaseRarity(); }
static bool RarityLower(const LootListItem *a, uint32_t b) { return a->CaseRarity() < b; }
static bool RarityUpper(uint32_t a, const LootListItem *b) { return a < b->CaseRarity(); }

bool SouvenirOpening::OpenPackage(const CSOEconItem &package, CSOEconItem &item)
{
    const LootList *lootList = m_itemSchema.GetCrateLootList(package.def_index());
    if (!lootList)
    {
        Platform::Print("SouvenirOpening: no loot list for def_index %u\n", package.def_index());
        return false;
    }

    // collect all items from loot list (including sublists)
    std::vector<const LootListItem *> lootListItems;
    GetLootListItems(*lootList, lootListItems);

    if (lootListItems.empty())
    {
        Platform::Print("SouvenirOpening: loot list is empty\n");
        return false;
    }

    // must be sorted for SelectItem
    std::sort(lootListItems.begin(), lootListItems.end(), CompareRarity);

    // select a random item with rarity-based weighting
    const LootListItem *selectedItem = SelectItem(lootListItems);
    if (!selectedItem)
    {
        Platform::Print("SouvenirOpening: failed to select item\n");
        return false;
    }

    // create the item with souvenir quality
    if (!m_itemSchema.CreateItemFromLootListItem(m_random, *selectedItem, false, ItemOriginCrate, UnacknowledgedTournamentDrop, item))
    {
        Platform::Print("SouvenirOpening: failed to create item\n");
        return false;
    }

    // override quality to tournament
    item.set_quality(ItemSchema::QualityTournament);

    // read tournament attributes from the package
    uint32_t eventId = 0;
    uint32_t teamId1 = 0;
    uint32_t teamId2 = 0;

    for (const CSOEconItemAttribute &attribute : package.attribute())
    {
        switch (attribute.def_index())
        {
        case ItemSchema::AttributeTournamentEventId:
            eventId = m_itemSchema.AttributeUint32(&attribute);
            break;

        case ItemSchema::AttributeTournamentTeamId1:
            teamId1 = m_itemSchema.AttributeUint32(&attribute);
            break;

        case ItemSchema::AttributeTournamentTeamId2:
            teamId2 = m_itemSchema.AttributeUint32(&attribute);
            break;
        }
    }

    ApplyTournamentAttributes(item, eventId, teamId1, teamId2, 0);

    return true;
}

const LootListItem *SouvenirOpening::SelectItem(const std::vector<const LootListItem *> &items)
{
    if (items.empty())
    {
        return nullptr;
    }

    // rarity-based selection like case opening
    std::vector<RarityWeight> weights;
    weights.reserve(items.size());

    float totalWeight = 0;

    for (size_t i = 0; i < items.size(); i++)
    {
        uint32_t rarity = items[i]->CaseRarity();
        float weight = GetConfig().GetRarityWeight(rarity);

        weights.push_back({ rarity, weight });
        totalWeight += weight;

        // skip over any duplicate rarities
        while (i + 1 < items.size() && items[i]->CaseRarity() == items[i + 1]->CaseRarity())
        {
            i++;
        }
    }

    // roll for rarity
    float value = m_random.Float(0.0f, totalWeight);
    float accum = 0.0f;

    for (const RarityWeight &pair : weights)
    {
        accum += pair.weight;
        if (value < accum)
        {
            // find items of this rarity
            auto lower = std::lower_bound(items.begin(), items.end(), pair.rarity, RarityLower);
            auto upper = std::upper_bound(lower, items.end(), pair.rarity, RarityUpper);

            size_t begin = std::distance(items.begin(), lower);
            size_t end = std::distance(items.begin(), upper);

            if (begin == end)
            {
                continue;
            }

            size_t index = m_random.Integer(begin, end - 1);
            return items[index];
        }
    }

    return nullptr;
}

void SouvenirOpening::ApplyTournamentAttributes(CSOEconItem &item, uint32_t eventId, uint32_t teamId1, uint32_t teamId2, uint32_t mvpAccountId)
{
    // add tournament event sticker (gold)
    CSOEconItemAttribute *attribute = item.add_attribute();
    attribute->set_def_index(ItemSchema::AttributeStickerId0);
    m_itemSchema.SetAttributeUint32(attribute, eventId);

    // add team 1 sticker (gold)
    attribute = item.add_attribute();
    attribute->set_def_index(ItemSchema::AttributeStickerId1);
    m_itemSchema.SetAttributeUint32(attribute, teamId1);

    // add team 2 sticker (gold)
    attribute = item.add_attribute();
    attribute->set_def_index(ItemSchema::AttributeStickerId2);
    m_itemSchema.SetAttributeUint32(attribute, teamId2);

    // add MVP autograph sticker (gold) if available
    if (mvpAccountId != 0)
    {
        attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeStickerId3);
        m_itemSchema.SetAttributeUint32(attribute, mvpAccountId);
    }
}
