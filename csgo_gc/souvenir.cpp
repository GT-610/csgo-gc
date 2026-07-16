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

static bool EndsWith(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

template<typename Predicate>
static const StickerKitInfo *SelectStickerKit(const std::vector<const StickerKitInfo *> &candidates, Predicate predicate, Random &random)
{
    std::vector<const StickerKitInfo *> matches;
    for (const StickerKitInfo *candidate : candidates)
    {
        if (predicate(*candidate))
        {
            matches.push_back(candidate);
        }
    }

    if (matches.empty())
    {
        return nullptr;
    }

    std::sort(matches.begin(), matches.end(), [](const StickerKitInfo *a, const StickerKitInfo *b) {
        return a->m_defIndex < b->m_defIndex;
    });

    return matches[random.Integer<size_t>(0, matches.size() - 1)];
}

static const StickerKitInfo *SelectHighestDefIndex(const std::vector<const StickerKitInfo *> &candidates)
{
    if (candidates.empty())
    {
        return nullptr;
    }

    return *std::max_element(candidates.begin(), candidates.end(), [](const StickerKitInfo *a, const StickerKitInfo *b) {
        return a->m_defIndex < b->m_defIndex;
    });
}

static const StickerKitInfo *SelectEventStickerKit(const ItemSchema &schema, uint32_t eventId, Random &random)
{
    std::vector<const StickerKitInfo *> candidates;
    schema.GetStickerKitsByTournamentEventId(eventId, candidates);

    // The first four Majors predate the later all-gold souvenir format.
    // Their dedicated souvenir variants are still identifiable from the
    // schema, but use different finishes.
    if (eventId == 1)
    {
        return SelectStickerKit(candidates, [](const StickerKitInfo &) { return true; }, random);
    }

    if (eventId == 3)
    {
        return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
            return kit.m_name.find("_gold_") != std::string::npos;
        }, random);
    }

    if (eventId == 4)
    {
        // Cologne 2014 has one additional event-only sticker after the two
        // capsule variants. Its definition index is the highest of the three.
        return SelectHighestDefIndex(candidates);
    }

    if (eventId == 5)
    {
        return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
            return !EndsWith(kit.m_name, "_foil");
        }, random);
    }

    return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
        return EndsWith(kit.m_name, "_gold");
    }, random);
}

static const StickerKitInfo *SelectTeamStickerKit(const ItemSchema &schema, uint32_t eventId, uint32_t teamId, Random &random)
{
    std::vector<const StickerKitInfo *> candidates;
    schema.GetStickerKitsByTournamentTeamId(eventId, teamId, candidates);

    const char *suffix = eventId <= 4 ? "_foil" : "_gold";
    return SelectStickerKit(candidates, [suffix](const StickerKitInfo &kit) {
        return EndsWith(kit.m_name, suffix);
    }, random);
}

static const StickerKitInfo *SelectPlayerStickerKit(const ItemSchema &schema, uint32_t eventId, uint32_t playerId, Random &random)
{
    std::vector<const StickerKitInfo *> candidates;
    schema.GetStickerKitsByTournamentPlayerId(eventId, playerId, candidates);
    return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
        return EndsWith(kit.m_name, "_gold");
    }, random);
}

static const StickerKitInfo *MapStickerKit(const ItemSchema &schema, const ItemInfo *packageInfo)
{
    if (!packageInfo)
    {
        return nullptr;
    }

    size_t mapPosition = packageInfo->m_name.rfind("_de_");
    if (mapPosition == std::string::npos)
    {
        return nullptr;
    }

    std::string stickerName = packageInfo->m_name.substr(mapPosition + 1);
    stickerName += "_gold";
    return schema.StickerKitInfoByName(stickerName);
}

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

    // Static tournament metadata comes from the package definition. Instance
    // attributes identify the particular match and override those defaults.
    const ItemInfo *packageInfo = m_itemSchema.ItemInfoByDefIndex(package.def_index());
    uint32_t eventId = packageInfo ? packageInfo->m_tournamentEventId : 0;
    uint32_t stageId = packageInfo ? packageInfo->m_tournamentEventStageId : 0;
    uint32_t team0Id = packageInfo ? packageInfo->m_tournamentTeam0Id : 0;
    uint32_t team1Id = packageInfo ? packageInfo->m_tournamentTeam1Id : 0;
    uint32_t mvpAccountId = 0;

    for (const CSOEconItemAttribute &attribute : package.attribute())
    {
        uint32_t value = m_itemSchema.AttributeUint32(&attribute);

        switch (attribute.def_index())
        {
        case ItemSchema::AttributeTournamentEventId:
            eventId = value;
            break;

        case ItemSchema::AttributeTournamentEventStageId:
            stageId = value;
            break;

        case ItemSchema::AttributeTournamentTeam0Id:
            team0Id = value;
            break;

        case ItemSchema::AttributeTournamentTeam1Id:
            team1Id = value;
            break;

        case ItemSchema::AttributeTournamentMvpAccountId:
            mvpAccountId = value;
            break;
        }
    }

    auto addTournamentAttribute = [&](uint32_t attributeDefIndex, uint32_t value)
    {
        if (!value)
        {
            return;
        }

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(attributeDefIndex);
        m_itemSchema.SetAttributeUint32(attribute, value);
    };

    addTournamentAttribute(ItemSchema::AttributeTournamentEventId, eventId);
    addTournamentAttribute(ItemSchema::AttributeTournamentEventStageId, stageId);
    addTournamentAttribute(ItemSchema::AttributeTournamentTeam0Id, team0Id);
    addTournamentAttribute(ItemSchema::AttributeTournamentTeam1Id, team1Id);
    addTournamentAttribute(ItemSchema::AttributeTournamentMvpAccountId, mvpAccountId);

    const StickerKitInfo *eventKit = eventId ? SelectEventStickerKit(m_itemSchema, eventId, m_random) : nullptr;
    const StickerKitInfo *team0Kit = eventId && team0Id ? SelectTeamStickerKit(m_itemSchema, eventId, team0Id, m_random) : nullptr;
    const StickerKitInfo *team1Kit = eventId && team1Id ? SelectTeamStickerKit(m_itemSchema, eventId, team1Id, m_random) : nullptr;
    const StickerKitInfo *fourthKit = nullptr;

    if (eventId >= 18)
    {
        fourthKit = MapStickerKit(m_itemSchema, packageInfo);
    }
    else if (eventId >= 7 && mvpAccountId)
    {
        fourthKit = SelectPlayerStickerKit(m_itemSchema, eventId, mvpAccountId, m_random);
    }

    uint32_t eventStickerKit = eventKit ? eventKit->m_defIndex : 0;
    uint32_t team0StickerKit = team0Kit ? team0Kit->m_defIndex : 0;
    uint32_t team1StickerKit = team1Kit ? team1Kit->m_defIndex : 0;
    uint32_t fourthStickerKit = fourthKit ? fourthKit->m_defIndex : 0;

    Platform::Print("SouvenirOpening: event %u stage %u teams %u/%u mvp %u stickers %u/%u/%u/%u\n",
        eventId, stageId, team0Id, team1Id, mvpAccountId,
        eventStickerKit, team0StickerKit, team1StickerKit, fourthStickerKit);

    ApplyTournamentAttributes(item, eventStickerKit, team0StickerKit, team1StickerKit, fourthStickerKit);

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

void SouvenirOpening::ApplyTournamentAttributes(CSOEconItem &item, uint32_t eventStickerKit, uint32_t team1StickerKit, uint32_t team2StickerKit, uint32_t fourthStickerKit)
{
    // Souvenir stickers are mint condition with default scale and rotation.
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

    // MVP autograph (2015-2019) or map gold sticker (2021+) in slot 3.
    if (fourthStickerKit != 0)
    {
        addSticker(ItemSchema::AttributeStickerId3, ItemSchema::AttributeStickerWear3,
            ItemSchema::AttributeStickerScale3, ItemSchema::AttributeStickerRotation3, fourthStickerKit);
    }
}
