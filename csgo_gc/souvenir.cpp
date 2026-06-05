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

bool SouvenirOpening::OpenPackage(const CSOEconItem &package, CSOEconItem &item)
{
    const LootList *lootList = m_itemSchema.GetCrateLootList(package.def_index());
    if (!lootList)
    {
        Platform::Print("SouvenirOpening: no loot list for def_index %u\n", package.def_index());
        return false;
    }

    // collect all items from loot list
    std::vector<const LootListItem *> lootListItems;
    for (const LootListItem &lootItem : lootList->items)
    {
        lootListItems.push_back(&lootItem);
    }

    if (lootListItems.empty())
    {
        Platform::Print("SouvenirOpening: loot list is empty\n");
        return false;
    }

    // select a random item
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

    return true;
}

const LootListItem *SouvenirOpening::SelectItem(const std::vector<const LootListItem *> &items)
{
    if (items.empty())
    {
        return nullptr;
    }

    // for now, just select a random item
    // TODO: implement rarity-based selection like case opening
    size_t index = m_random.Integer<size_t>(0, items.size() - 1);
    return items[index];
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
