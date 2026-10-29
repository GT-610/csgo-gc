#include "stdafx.h"
#include "item_schema.h"
#include "keyvalue.h"

namespace Platform
{

void Print(const char *, ...)
{
}

}

class ItemSchemaTestFixture
{
public:
    ItemSchemaTestFixture()
        : schema{ false }
    {
        KeyValue tournamentEventAttribute{ "137" };
        tournamentEventAttribute.AddNumber("stored_as_integer", 1);
        schema.m_attributeInfo.emplace(
            ItemSchema::AttributeTournamentEventId,
            AttributeInfo{ tournamentEventAttribute });

        schema.m_itemInfo.reserve(2);
        schema.m_lootLists.reserve(4);
    }

    void AddTournamentStickerCapsule(uint32_t defIndex)
    {
        ItemInfo &itemInfo = schema.m_itemInfo.try_emplace(defIndex, defIndex).first->second;
        itemInfo.m_lootListName = "tournament_capsule";

        LootList &lootList = schema.m_lootLists["tournament_capsule"];
        lootList.items.emplace_back().type = LootListItemSticker;
    }

    void AddNestedSouvenirPackage(uint32_t defIndex)
    {
        ItemInfo &itemInfo = schema.m_itemInfo.try_emplace(defIndex, defIndex).first->second;
        itemInfo.m_supplyCrateSeries = 1;
        itemInfo.m_prefabs.push_back("weapon_case_souvenirpkg");

        LootList &paintedItems = schema.m_lootLists["painted_items"];
        paintedItems.items.emplace_back().type = LootListItemPaintable;

        LootList &nestedItems = schema.m_lootLists["nested_items"];
        nestedItems.subLists.push_back(&paintedItems);

        LootList &packageContents = schema.m_lootLists["package_contents"];
        packageContents.subLists.push_back(&nestedItems);

        schema.m_revolvingLootLists.try_emplace(1, packageContents);
    }

    bool ParseStickerSlotCounts()
    {
        KeyValue prefabs{ "prefabs" };

        KeyValue &fourSlotPrefab = prefabs.AddSubkey("four_slot_weapon");
        KeyValue &fourSlots = fourSlotPrefab.AddSubkey("stickers");
        fourSlots.AddSubkey("0");
        fourSlots.AddSubkey("1");
        fourSlots.AddSubkey("2");
        fourSlots.AddSubkey("3");

        KeyValue &fiveSlotPrefab = prefabs.AddSubkey("five_slot_weapon");
        fiveSlotPrefab.AddString("prefab", "four_slot_weapon");
        fiveSlotPrefab.AddSubkey("stickers").AddSubkey("4");

        KeyValue items{ "items" };
        items.AddSubkey("7").AddString("prefab", "four_slot_weapon");
        items.AddSubkey("11").AddString("prefab", "five_slot_weapon");

        schema.ParseItems(&items, &prefabs);

        const ItemInfo *fourSlotItem = schema.ItemInfoByDefIndex(7);
        const ItemInfo *fiveSlotItem = schema.ItemInfoByDefIndex(11);
        return fourSlotItem && fiveSlotItem
            && fourSlotItem->m_stickerSlotCount == 4
            && fiveSlotItem->m_stickerSlotCount == 5;
    }

    bool ParsePaintKitWearRanges()
    {
        KeyValue paintKits{ "paint_kits" };

        KeyValue &defaultPaintKit = paintKits.AddSubkey("0");
        defaultPaintKit.AddString("name", "default");
        defaultPaintKit.AddNumber("wear_remap_min", 0.06f);
        defaultPaintKit.AddNumber("wear_remap_max", 0.8f);

        paintKits.AddSubkey("1").AddString("name", "inherited");

        KeyValue &overriddenPaintKit = paintKits.AddSubkey("2");
        overriddenPaintKit.AddString("name", "overridden");
        overriddenPaintKit.AddNumber("wear_remap_min", 0.2f);
        overriddenPaintKit.AddNumber("wear_remap_max", 0.4f);

        KeyValue &partiallyOverriddenPaintKit = paintKits.AddSubkey("3");
        partiallyOverriddenPaintKit.AddString("name", "partially_overridden");
        partiallyOverriddenPaintKit.AddNumber("wear_remap_max", 0.5f);

        schema.ParsePaintKits(&paintKits);

        const PaintKitInfo *inherited = schema.PaintKitInfoByDefIndex(1);
        const PaintKitInfo *overridden = schema.PaintKitInfoByDefIndex(2);
        const PaintKitInfo *partiallyOverridden = schema.PaintKitInfoByDefIndex(3);
        return inherited && overridden && partiallyOverridden
            && inherited->m_minFloat == 0.06f
            && inherited->m_maxFloat == 0.8f
            && overridden->m_minFloat == 0.2f
            && overridden->m_maxFloat == 0.4f
            && partiallyOverridden->m_minFloat == 0.06f
            && partiallyOverridden->m_maxFloat == 0.5f;
    }

    bool ParsePrestigeMedals()
    {
        KeyValue prefabs{ "prefabs" };
        prefabs.AddSubkey("prestige_coin").AddString("item_type", "prestige_coin");

        KeyValue items{ "items" };
        auto addMedal = [&](const char *defIndex, uint32_t year)
        {
            KeyValue &item = items.AddSubkey(defIndex);
            item.AddString("prefab", "prestige_coin");
            item.AddSubkey("attributes").AddNumber("prestige year", year);
        };

        addMedal("5002", 2023);
        addMedal("5000", 2023);
        addMedal("4999", 2022);
        items.AddSubkey("5001").AddSubkey("attributes").AddNumber("prestige year", 2023);

        schema.ParseItems(&items, &prefabs);

        const ItemInfo *medal = schema.ItemInfoByDefIndex(5000);
        const std::vector<uint32_t> expected{ 5000, 5002 };
        return medal
            && medal->m_itemType == "prestige_coin"
            && medal->m_prestigeYear == 2023
            && schema.PrestigeMedalDefIndexes(2023) == expected;
    }

    bool ParseTournamentAccessItems()
    {
        KeyValue attributes{ "attributes" };
        auto addIntegerAttribute = [&](uint32_t defIndex, const char *name)
        {
            KeyValue &attribute = attributes.AddSubkey(std::to_string(defIndex));
            attribute.AddString("name", name);
            attribute.AddNumber("stored_as_integer", 1);
        };
        addIntegerAttribute(ItemSchema::AttributeStickerId0, "sticker slot 0 id");
        addIntegerAttribute(ItemSchema::AttributeTournamentEventId, "tournament event id");
        addIntegerAttribute(ItemSchema::AttributeCampaignCompletionBitfield,
            "campaign completion bitfield");
        addIntegerAttribute(ItemSchema::AttributeOperationDropsAwardedPurchased,
            "operation drops awarded 1");
        addIntegerAttribute(ItemSchema::AttributeOperationDropsAwardedRedeemed,
            "operation drops awarded 0");
        schema.ParseAttributes(&attributes);

        KeyValue prefabs{ "prefabs" };
        prefabs.AddSubkey("fan_token");
        prefabs.AddSubkey("fan_shield");

        KeyValue &passPrefab = prefabs.AddSubkey("paris_pass");
        passPrefab.AddString("prefab", "fan_token");
        KeyValue &passEvent = passPrefab.AddSubkey("attributes")
            .AddSubkey("tournament event id");
        passEvent.AddNumber("value", 21);

        KeyValue &journalPrefab = prefabs.AddSubkey("paris_journal");
        journalPrefab.AddString("prefab", "fan_shield");
        KeyValue &journalAttributes = journalPrefab.AddSubkey("attributes");
        journalAttributes.AddSubkey("tournament event id").AddNumber("value", 21);
        auto addGenerated = [&](const char *name, uint32_t value)
        {
            KeyValue &attribute = journalAttributes.AddSubkey(name);
            attribute.AddNumber("value", value);
            attribute.AddNumber("force_gc_to_generate", 1);
        };
        addGenerated("sticker slot 0 id", 6732);
        addGenerated("campaign completion bitfield", 1);
        addGenerated("operation drops awarded 1", 0);
        addGenerated("operation drops awarded 0", 0);

        KeyValue items{ "items" };
        auto addItem = [&](const char *defIndex, const char *name, const char *prefab)
        {
            KeyValue &item = items.AddSubkey(defIndex);
            item.AddString("name", name);
            item.AddString("prefab", prefab);
        };
        addItem("100", "tournament_pass_paris2023", "paris_pass");
        addItem("101", "tournament_pass_paris2023_pack", "paris_pass");
        addItem("102", "tournament_pass_paris2023_charge", "paris_pass");
        addItem("200", "tournament_journal_paris2023", "paris_journal");
        schema.ParseItems(&items, &prefabs);

        auto pass = schema.TournamentAccessByDefIndex(100);
        auto pack = schema.TournamentAccessByDefIndex(101);
        auto token = schema.TournamentAccessByDefIndex(102);
        if (!pass || !pack || !token
            || pass->type != TournamentAccessType::Pass
            || pack->type != TournamentAccessType::PassWithTokens
            || pack->includedTokens != 3
            || token->type != TournamentAccessType::Token
            || pass->eventId != 21 || pass->journalDefIndex != 200
            || pack->journalDefIndex != 200 || token->journalDefIndex != 200)
        {
            return false;
        }

        CSOEconItem journal;
        if (!schema.CreateItem(200, ItemOriginPurchased, UnacknowledgedPurchased, journal))
        {
            return false;
        }

        auto generatedValue = [&](uint32_t defIndex) -> std::optional<uint32_t>
        {
            for (const CSOEconItemAttribute &attribute : journal.attribute())
            {
                if (attribute.def_index() == defIndex)
                {
                    return schema.AttributeUint32(&attribute);
                }
            }
            return std::nullopt;
        };

        return journal.attribute_size() == 4
            && generatedValue(ItemSchema::AttributeStickerId0) == 6732
            && generatedValue(ItemSchema::AttributeCampaignCompletionBitfield) == 1
            && generatedValue(ItemSchema::AttributeOperationDropsAwardedPurchased) == 0
            && generatedValue(ItemSchema::AttributeOperationDropsAwardedRedeemed) == 0;
    }

    bool ParseSeasonalOperationData()
    {
        KeyValue prefabs{ "prefabs" };
        KeyValue &seasonPass = prefabs.AddSubkey("season_pass");
        seasonPass.AddSubkey("tool").AddString("type", "season_pass");
        prefabs.AddSubkey("operation_coin");
        KeyValue &seasonCoin = prefabs.AddSubkey("season11_coin");
        seasonCoin.AddString("prefab", "operation_coin");
        seasonCoin.AddSubkey("attributes").AddNumber("season access", 10);

        KeyValue items{ "items" };
        KeyValue &pass = items.AddSubkey("4758");
        pass.AddString("prefab", "season_pass");
        pass.AddSubkey("attributes").AddNumber("season access", 10);

        KeyValue &bronzeCoin = items.AddSubkey("4759");
        bronzeCoin.AddString("prefab", "season11_coin");
        bronzeCoin.AddNumber("min_ilevel", 1);
        bronzeCoin.AddNumber("max_ilevel", 1);

        KeyValue &silverCoin = items.AddSubkey("4760");
        silverCoin.AddString("prefab", "season11_coin");
        silverCoin.AddNumber("min_ilevel", 2);
        silverCoin.AddNumber("max_ilevel", 2);

        schema.ParseItems(&items, &prefabs);

        KeyValue seasonalOperations{ "seasonaloperations" };
        KeyValue &riptide = seasonalOperations.AddSubkey("10");
        riptide.AddSubkey("quest_mission_card").AddNumber("id", 9051);
        riptide.AddSubkey("quest_mission_card").AddNumber("id", 9052);
        riptide.AddString("xp_reward", "7,11,15");
        seasonalOperations.AddSubkey("11").AddString("xp_reward", "7,11,15");
        seasonalOperations.AddSubkey("12").AddSubkey("quest_mission_card").AddNumber("id", 0);
        schema.ParseSeasonalOperations(&seasonalOperations);

        std::optional<SeasonPassInfo> info = schema.SeasonPassByDefIndex(4758);
        return info
            && info->seasonValue == 10
            && info->coinDefIndex == 4759
            && !schema.SeasonPassByDefIndex(4759)
            && schema.IsSeasonalMissionCard(10, 9051)
            && schema.IsSeasonalMissionCard(10, 9052)
            && !schema.IsSeasonalMissionCard(10, 9053)
            && !schema.IsSeasonalMissionCard(9, 9051)
            && schema.m_missionCardsBySeason.find(11) == schema.m_missionCardsBySeason.end()
            && schema.m_missionCardsBySeason.find(12) == schema.m_missionCardsBySeason.end();
    }

    bool ValidateCouponStatTrakTargets()
    {
        auto addUint32Attribute = [&](uint32_t defIndex)
        {
            KeyValue attribute{ std::to_string(defIndex) };
            attribute.AddString("attribute_type", "uint32");
            schema.m_attributeInfo.emplace(defIndex, AttributeInfo{ attribute });
        };
        addUint32Attribute(ItemSchema::AttributeTexturePrefab);
        addUint32Attribute(ItemSchema::AttributeTextureSeed);
        addUint32Attribute(ItemSchema::AttributeTextureWear);
        addUint32Attribute(ItemSchema::AttributeKillEater);
        addUint32Attribute(ItemSchema::AttributeKillEaterScoreType);

        ItemInfo &target = schema.m_itemInfo.try_emplace(100, 100).first->second;
        target.m_name = "weapon_test";

        ItemInfo &validCoupon = schema.m_itemInfo.try_emplace(200, 200).first->second;
        validCoupon.m_name = "coupon_stattrak_valid";
        validCoupon.m_isCoupon = true;
        validCoupon.m_lootListName = "stattrak_valid";
        validCoupon.m_willProduceStatTrak = true;

        ItemInfo &invalidCoupon = schema.m_itemInfo.try_emplace(201, 201).first->second;
        invalidCoupon.m_name = "coupon_stattrak_invalid";
        invalidCoupon.m_isCoupon = true;
        invalidCoupon.m_lootListName = "stattrak_invalid";
        invalidCoupon.m_willProduceStatTrak = true;

        KeyValue paintKitKey{ "1" };
        paintKitKey.AddString("name", "paint_test");
        auto paintKit = schema.m_paintKitInfo.emplace("paint_test",
            PaintKitInfo{ paintKitKey, 0.0f, 1.0f });

        const ItemInfo *targetInfo = schema.ItemInfoByDefIndex(100);
        LootListItem validLootItem;
        validLootItem.itemInfo = targetInfo;
        validLootItem.type = LootListItemPaintable;
        validLootItem.paintKitInfo = &paintKit.first->second;
        validLootItem.rarity = ItemSchema::RarityCommon;
        validLootItem.quality = ItemSchema::QualityUnique;
        schema.m_lootLists["stattrak_valid"].items.push_back(validLootItem);

        LootListItem invalidLootItem;
        invalidLootItem.itemInfo = targetInfo;
        invalidLootItem.type = LootListItemNoAttribute;
        invalidLootItem.rarity = ItemSchema::RarityCommon;
        invalidLootItem.quality = ItemSchema::QualityUnique;
        schema.m_lootLists["stattrak_invalid"].items.push_back(invalidLootItem);

        if (!schema.CanCreateItem(200) || schema.CanCreateItem(201))
        {
            return false;
        }

        CSOEconItem created;
        if (!schema.CreateItem(200, ItemOriginPurchased, UnacknowledgedPurchased, created)
            || created.quality() != ItemSchema::QualityStrange)
        {
            return false;
        }

        bool hasKillEater = false;
        bool hasWeaponScoreType = false;
        for (const CSOEconItemAttribute &attribute : created.attribute())
        {
            hasKillEater |= attribute.def_index() == ItemSchema::AttributeKillEater;
            hasWeaponScoreType |= attribute.def_index() == ItemSchema::AttributeKillEaterScoreType
                && schema.AttributeUint32(&attribute) == 0;
        }

        return hasKillEater && hasWeaponScoreType;
    }

    ItemSchema schema;
};

static bool TournamentStickerCapsuleIsNotSouvenir()
{
    ItemSchemaTestFixture fixture;
    fixture.AddTournamentStickerCapsule(9000);

    CSOEconItem capsule;
    capsule.set_def_index(9000);
    capsule.set_quality(ItemSchema::QualityTournament);

    CSOEconItemAttribute *eventAttribute = capsule.add_attribute();
    eventAttribute->set_def_index(ItemSchema::AttributeTournamentEventId);
    fixture.schema.SetAttributeUint32(eventAttribute, 21);

    return !fixture.schema.IsSouvenirPackage(capsule);
}

static bool NestedPaintedLootListIsSouvenir()
{
    ItemSchemaTestFixture fixture;
    fixture.AddNestedSouvenirPackage(9001);

    CSOEconItem package;
    package.set_def_index(9001);
    package.set_quality(ItemSchema::QualityUnique);

    return fixture.schema.IsSouvenirPackage(package);
}

static bool StickerSlotCountsFollowPrefabData()
{
    ItemSchemaTestFixture fixture;
    return fixture.ParseStickerSlotCounts();
}

static bool PaintKitWearRangesInheritDefaults()
{
    ItemSchemaTestFixture fixture;
    return fixture.ParsePaintKitWearRanges();
}

static bool PrestigeMedalsUseSchemaYearAndSortedDefIndexes()
{
    ItemSchemaTestFixture fixture;
    return fixture.ParsePrestigeMedals();
}

static bool TournamentAccessUsesSchemaMetadataAndGeneratedAttributes()
{
    ItemSchemaTestFixture fixture;
    return fixture.ParseTournamentAccessItems();
}

static bool SeasonalOperationUsesSchemaPassCoinAndMissionCards()
{
    ItemSchemaTestFixture fixture;
    return fixture.ParseSeasonalOperationData();
}

static bool CouponStatTrakValidationMatchesTargetType()
{
    ItemSchemaTestFixture fixture;
    return fixture.ValidateCouponStatTrakTargets();
}

int main()
{
    if (!TournamentStickerCapsuleIsNotSouvenir())
    {
        return 1;
    }

    if (!NestedPaintedLootListIsSouvenir())
    {
        return 2;
    }

    if (!StickerSlotCountsFollowPrefabData())
    {
        return 3;
    }

    if (!PaintKitWearRangesInheritDefaults())
    {
        return 4;
    }

    if (!PrestigeMedalsUseSchemaYearAndSortedDefIndexes())
    {
        return 5;
    }

    if (!TournamentAccessUsesSchemaMetadataAndGeneratedAttributes())
    {
        return 6;
    }

    if (!SeasonalOperationUsesSchemaPassCoinAndMissionCards())
    {
        return 7;
    }

    if (!CouponStatTrakValidationMatchesTargetType())
    {
        return 8;
    }

    return 0;
}
