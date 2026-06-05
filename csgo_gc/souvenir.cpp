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

    // read tournament attributes from the package and look up sticker kits
    uint32_t eventId = 0;
    uint32_t teamId1 = 0;
    uint32_t teamId2 = 0;

    for (const CSOEconItemAttribute &attribute : package.attribute())
    {
        uint32_t value = m_itemSchema.AttributeUint32(&attribute);

        switch (attribute.def_index())
        {
        case ItemSchema::AttributeTournamentEventId:
            eventId = value;
            break;

        case ItemSchema::AttributeTournamentTeamId1:
            teamId1 = value;
            break;

        case ItemSchema::AttributeTournamentTeamId2:
            teamId2 = value;
            break;
        }
    }

    const StickerKitInfo *eventKit = eventId ? m_itemSchema.StickerKitByTournamentEventId(eventId) : nullptr;
    const StickerKitInfo *team1Kit = eventId && teamId1 ? m_itemSchema.StickerKitByTournamentTeamId(eventId, teamId1) : nullptr;
    const StickerKitInfo *team2Kit = eventId && teamId2 ? m_itemSchema.StickerKitByTournamentTeamId(eventId, teamId2) : nullptr;

    uint32_t eventStickerKit = eventKit ? eventKit->m_defIndex : 0;
    uint32_t team1StickerKit = team1Kit ? team1Kit->m_defIndex : 0;
    uint32_t team2StickerKit = team2Kit ? team2Kit->m_defIndex : 0;

    ApplyTournamentAttributes(item, eventStickerKit, team1StickerKit, team2StickerKit, 0);

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

void SouvenirOpening::ApplyTournamentAttributes(CSOEconItem &item, uint32_t eventStickerKit, uint32_t team1StickerKit, uint32_t team2StickerKit, uint32_t mvpStickerKit)
{
    // souvenir stickers are always gold, mint condition, default scale, no rotation
    auto addSticker = [&](uint32_t stickerIdAttr, uint32_t wearAttr, uint32_t scaleAttr, uint32_t rotationAttr, uint32_t stickerKitDefIndex)
    {
        if (stickerKitDefIndex == 0)
        {
            return;
        }

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(stickerIdAttr);
        m_itemSchema.SetAttributeUint32(attribute, stickerKitDefIndex);

        attribute = item.add_attribute();
        attribute->set_def_index(wearAttr);
        m_itemSchema.SetAttributeFloat(attribute, 0.0f);

        attribute = item.add_attribute();
        attribute->set_def_index(scaleAttr);
        m_itemSchema.SetAttributeFloat(attribute, 1.0f);

        attribute = item.add_attribute();
        attribute->set_def_index(rotationAttr);
        m_itemSchema.SetAttributeFloat(attribute, 0.0f);
    };

    // event sticker (slot 0)
    addSticker(ItemSchema::AttributeStickerId0, ItemSchema::AttributeStickerWear0,
        ItemSchema::AttributeStickerScale0, ItemSchema::AttributeStickerRotation0, eventStickerKit);

    // team 1 sticker (slot 1)
    addSticker(ItemSchema::AttributeStickerId1, ItemSchema::AttributeStickerWear1,
        ItemSchema::AttributeStickerScale1, ItemSchema::AttributeStickerRotation1, team1StickerKit);

    // team 2 sticker (slot 2)
    addSticker(ItemSchema::AttributeStickerId2, ItemSchema::AttributeStickerWear2,
        ItemSchema::AttributeStickerScale2, ItemSchema::AttributeStickerRotation2, team2StickerKit);

    // MVP autograph sticker (slot 3) if available
    if (mvpStickerKit != 0)
    {
        addSticker(ItemSchema::AttributeStickerId3, ItemSchema::AttributeStickerWear3,
            ItemSchema::AttributeStickerScale3, ItemSchema::AttributeStickerRotation3, mvpStickerKit);
    }
}
