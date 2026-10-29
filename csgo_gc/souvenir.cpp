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

namespace SouvenirTournament
{
    constexpr uint32_t DreamHack2013 = 1;
    constexpr uint32_t Katowice2014 = 3;
    constexpr uint32_t Cologne2014 = 4;
    constexpr uint32_t DreamHack2014 = 5;
    constexpr uint32_t Katowice2015 = 6;
    constexpr uint32_t Cologne2015 = 7;
    constexpr uint32_t Berlin2019 = 16;
    constexpr uint32_t Stockholm2021 = 18;
    constexpr uint32_t Paris2023 = 21;
}

namespace DreamHack2013Sticker
{
    constexpr float MinWear = 0.2f;
    constexpr float MaxWear = 0.3f;
    constexpr float MinScale = 1.0f;
    constexpr float MaxScale = 1.2f;
    constexpr float MinRotation = -10.0f;
    constexpr float MaxRotation = 10.0f;
}

enum class SouvenirFormat
{
    Unknown,
    EventOnly,
    FoilTeams,
    GoldTeams,
    GoldMvp,
    GoldMap,
};

static SouvenirFormat SouvenirFormatForEvent(uint32_t eventId)
{
    if (eventId == SouvenirTournament::DreamHack2013)
    {
        return SouvenirFormat::EventOnly;
    }

    if (eventId >= SouvenirTournament::Katowice2014 && eventId <= SouvenirTournament::Cologne2014)
    {
        return SouvenirFormat::FoilTeams;
    }

    if (eventId >= SouvenirTournament::DreamHack2014 && eventId <= SouvenirTournament::Katowice2015)
    {
        return SouvenirFormat::GoldTeams;
    }

    if (eventId >= SouvenirTournament::Cologne2015 && eventId <= SouvenirTournament::Berlin2019)
    {
        return SouvenirFormat::GoldMvp;
    }

    if (eventId >= SouvenirTournament::Stockholm2021 && eventId <= SouvenirTournament::Paris2023)
    {
        return SouvenirFormat::GoldMap;
    }

    return SouvenirFormat::Unknown;
}

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

static const StickerKitInfo *SelectEventStickerKit(
    const ItemSchema &schema, uint32_t eventId, SouvenirFormat format, Random &random)
{
    std::vector<const StickerKitInfo *> candidates = schema.TournamentStickerKits(TournamentStickerRole::Event, eventId);

    if (format == SouvenirFormat::EventOnly)
    {
        return SelectStickerKit(candidates, [](const StickerKitInfo &) { return true; }, random);
    }

    if (eventId == SouvenirTournament::Katowice2014)
    {
        return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
            return kit.m_name.find("_gold_") != std::string::npos;
        }, random);
    }

    if (eventId == SouvenirTournament::Cologne2014)
    {
        return schema.FindStickerKitInfoByName("cologne2014_esl_c");
    }

    if (eventId == SouvenirTournament::DreamHack2014)
    {
        return schema.FindStickerKitInfoByName("dhw2014_dhw");
    }

    return SelectStickerKit(candidates, [](const StickerKitInfo &kit) {
        return EndsWith(kit.m_name, "_gold");
    }, random);
}

static const StickerKitInfo *SelectTeamStickerKit(
    const ItemSchema &schema, uint32_t eventId, uint32_t teamId, SouvenirFormat format, Random &random)
{
    std::vector<const StickerKitInfo *> candidates = schema.TournamentStickerKits(TournamentStickerRole::Team, eventId, teamId);

    const char *suffix = format == SouvenirFormat::FoilTeams ? "_foil" : "_gold";
    return SelectStickerKit(candidates, [suffix](const StickerKitInfo &kit) {
        return EndsWith(kit.m_name, suffix);
    }, random);
}

static const StickerKitInfo *SelectPlayerStickerKit(const ItemSchema &schema, uint32_t eventId, uint32_t playerId, Random &random)
{
    std::vector<const StickerKitInfo *> candidates = schema.TournamentStickerKits(TournamentStickerRole::Player, eventId, playerId);
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
    return schema.FindStickerKitInfoByName(stickerName);
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

    // Opening a souvenir package is a crate acquisition. TournamentDrop is
    // reserved for packages awarded directly for watching tournament matches.
    if (!m_itemSchema.CreateItemFromLootListItem(
            m_random,
            *selectedItem,
            false,
            ItemOriginCrate,
            UnacknowledgedFoundInCrate,
            item))
    {
        Platform::Print("SouvenirOpening: failed to create item\n");
        return false;
    }

    // override quality to tournament
    item.set_quality(ItemSchema::QualityTournament);

    // Static tournament metadata comes from the package definition. Instance
    // attributes identify the particular match and override those defaults.
    const ItemInfo *packageInfo = m_itemSchema.ItemInfoByDefIndex(package.def_index());
    TournamentMetadata tournament = packageInfo ? packageInfo->m_tournament : TournamentMetadata{};

    for (const CSOEconItemAttribute &attribute : package.attribute())
    {
        uint32_t value = m_itemSchema.AttributeUint32(&attribute);

        switch (attribute.def_index())
        {
        case ItemSchema::AttributeTournamentEventId:
            tournament.eventId = value;
            break;

        case ItemSchema::AttributeTournamentEventStageId:
            tournament.stageId = value;
            break;

        case ItemSchema::AttributeTournamentTeam0Id:
            tournament.team0Id = value;
            break;

        case ItemSchema::AttributeTournamentTeam1Id:
            tournament.team1Id = value;
            break;

        case ItemSchema::AttributeTournamentMvpAccountId:
            tournament.mvpAccountId = value;
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

    addTournamentAttribute(ItemSchema::AttributeTournamentEventId, tournament.eventId);
    addTournamentAttribute(ItemSchema::AttributeTournamentEventStageId, tournament.stageId);
    addTournamentAttribute(ItemSchema::AttributeTournamentTeam0Id, tournament.team0Id);
    addTournamentAttribute(ItemSchema::AttributeTournamentTeam1Id, tournament.team1Id);
    addTournamentAttribute(ItemSchema::AttributeTournamentMvpAccountId, tournament.mvpAccountId);

    SouvenirFormat format = SouvenirFormatForEvent(tournament.eventId);
    if (tournament.eventId && format == SouvenirFormat::Unknown)
    {
        Platform::Print("SouvenirOpening: unsupported tournament event %u\n", tournament.eventId);
    }

    const StickerKitInfo *eventKit = format != SouvenirFormat::Unknown
        ? SelectEventStickerKit(m_itemSchema, tournament.eventId, format, m_random)
        : nullptr;
    const StickerKitInfo *team0Kit = format != SouvenirFormat::Unknown && tournament.team0Id
        ? SelectTeamStickerKit(m_itemSchema, tournament.eventId, tournament.team0Id, format, m_random)
        : nullptr;
    const StickerKitInfo *team1Kit = format != SouvenirFormat::Unknown && tournament.team1Id
        ? SelectTeamStickerKit(m_itemSchema, tournament.eventId, tournament.team1Id, format, m_random)
        : nullptr;
    const StickerKitInfo *fourthKit = nullptr;

    if (format == SouvenirFormat::GoldMap)
    {
        fourthKit = MapStickerKit(m_itemSchema, packageInfo);
    }
    else if (format == SouvenirFormat::GoldMvp && tournament.mvpAccountId)
    {
        fourthKit = SelectPlayerStickerKit(m_itemSchema, tournament.eventId, tournament.mvpAccountId, m_random);
    }

    uint32_t eventStickerKit = eventKit ? eventKit->m_defIndex : 0;
    uint32_t team0StickerKit = team0Kit ? team0Kit->m_defIndex : 0;
    uint32_t team1StickerKit = team1Kit ? team1Kit->m_defIndex : 0;
    uint32_t fourthStickerKit = fourthKit ? fourthKit->m_defIndex : 0;

    const ItemInfo *itemInfo = m_itemSchema.ItemInfoByDefIndex(item.def_index());
    uint32_t stickerSlotCount = itemInfo ? itemInfo->m_stickerSlotCount : 0;

    Platform::Print("SouvenirOpening: event %u stage %u teams %u/%u mvp %u stickers %u/%u/%u/%u slots %u\n",
        tournament.eventId, tournament.stageId, tournament.team0Id, tournament.team1Id, tournament.mvpAccountId,
        eventStickerKit, team0StickerKit, team1StickerKit, fourthStickerKit, stickerSlotCount);

    if (!ApplyTournamentAttributes(item, stickerSlotCount, format == SouvenirFormat::EventOnly,
            eventStickerKit, team0StickerKit, team1StickerKit, fourthStickerKit))
    {
        return false;
    }

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

bool SouvenirOpening::ApplyTournamentAttributes(CSOEconItem &item,
    uint32_t stickerSlotCount,
    bool dreamHack2013,
    uint32_t eventStickerKit,
    uint32_t team1StickerKit,
    uint32_t team2StickerKit,
    uint32_t fourthStickerKit)
{
    std::vector<uint32_t> stickerKits;
    for (uint32_t stickerKit : { eventStickerKit, team1StickerKit, team2StickerKit, fourthStickerKit })
    {
        if (stickerKit != 0)
        {
            stickerKits.push_back(stickerKit);
        }
    }

    if (stickerKits.empty())
    {
        return true;
    }

    if (stickerSlotCount > static_cast<uint32_t>(MaxStickers)
        || stickerKits.size() > stickerSlotCount)
    {
        Platform::Print("SouvenirOpening: item def %u has %u sticker slots for %zu stickers\n",
            item.def_index(), stickerSlotCount, stickerKits.size());
        return false;
    }

    std::array<uint32_t, MaxStickers> slots{};
    for (uint32_t slot = 0; slot < stickerSlotCount; slot++)
    {
        slots[slot] = slot;
    }

    // Shuffle all valid slots, then use the first N. This produces a uniform
    // assignment without placing two stickers in the same slot.
    for (size_t count = stickerSlotCount; count > 1; count--)
    {
        size_t other = m_random.Integer<size_t>(0, count - 1);
        std::swap(slots[count - 1], slots[other]);
    }

    for (size_t i = 0; i < stickerKits.size(); i++)
    {
        uint32_t slot = slots[i];
        uint32_t stickerIdAttribute = ItemSchema::AttributeStickerId0 + slot * 4;
        uint32_t stickerWearAttribute = ItemSchema::AttributeStickerWear0 + slot * 4;
        uint32_t stickerScaleAttribute = ItemSchema::AttributeStickerScale0 + slot * 4;
        uint32_t stickerRotationAttribute = ItemSchema::AttributeStickerRotation0 + slot * 4;

        float wear = dreamHack2013
            ? m_random.Float(DreamHack2013Sticker::MinWear, DreamHack2013Sticker::MaxWear)
            : 0.0f;
        float scale = dreamHack2013
            ? m_random.Float(DreamHack2013Sticker::MinScale, DreamHack2013Sticker::MaxScale)
            : 1.0f;
        float rotation = dreamHack2013
            ? m_random.Float(DreamHack2013Sticker::MinRotation, DreamHack2013Sticker::MaxRotation)
            : 0.0f;

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(stickerIdAttribute);
        if (!m_itemSchema.SetAttributeUint32(attribute, stickerKits[i]))
        {
            return false;
        }

        attribute = item.add_attribute();
        attribute->set_def_index(stickerWearAttribute);
        if (!m_itemSchema.SetAttributeFloat(attribute, wear))
        {
            return false;
        }

        attribute = item.add_attribute();
        attribute->set_def_index(stickerScaleAttribute);
        if (!m_itemSchema.SetAttributeFloat(attribute, scale))
        {
            return false;
        }

        attribute = item.add_attribute();
        attribute->set_def_index(stickerRotationAttribute);
        if (!m_itemSchema.SetAttributeFloat(attribute, rotation))
        {
            return false;
        }
    }

    return true;
}
