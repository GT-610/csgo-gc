#include "stdafx.h"
#include "item_schema.h"
#include "config.h"
#include "keyvalue.h"
#include "random.h"
#include "game_schema_patch.h"
#include "star_shop.h"
#include <unordered_set>

// ideally this would get parsed from the item schema...
static uint32_t ItemRarityFromString(std::string_view name)
{
    const std::pair<std::string_view, uint32_t> rarityNames[] = {
        { "default", ItemSchema::RarityDefault },
        { "common", ItemSchema::RarityCommon },
        { "uncommon", ItemSchema::RarityUncommon },
        { "rare", ItemSchema::RarityRare },
        { "mythical", ItemSchema::RarityMythical },
        { "legendary", ItemSchema::RarityLegendary },
        { "ancient", ItemSchema::RarityAncient },
        { "immortal", ItemSchema::RarityImmortal },
        { "unusual", ItemSchema::RarityUnusual },
    };

    for (const auto &pair : rarityNames)
    {
        if (pair.first == name)
        {
            return pair.second;
        }
    }

    assert(false);
    return ItemSchema::RarityCommon;
}

AttributeInfo::AttributeInfo(const KeyValue &key)
{
    std::string_view type = key.GetString("attribute_type");
    if (type.size())
    {
        if (type == "float")
        {
            m_type = AttributeType::Float;
        }
        else if (type == "uint32")
        {
            m_type = AttributeType::Uint32;
        }
        else if (type == "string")
        {
            m_type = AttributeType::String;
        }
        else
        {
            // not supported, fall back to float
            Platform::Print("Unsupported attribute type %s\n", std::string{ type }.c_str());
            m_type = AttributeType::Float;
        }
    }
    else
    {
        bool integer = key.GetNumber<int>("stored_as_integer");
        m_type = integer ? AttributeType::Uint32 : AttributeType::Float;
    }
}

ItemInfo::ItemInfo(uint32_t defIndex)
    : m_defIndex{ defIndex }
    , m_rarity{ ItemSchema::RarityCommon }
    , m_quality{ ItemSchema::QualityNormal }
    , m_level{ 1 }
    , m_supplyCrateSeries{ 0 }
    , m_stickerSlotCount{ 0 }
    , m_prestigeYear{ 0 }
    , m_canSticker{ false }
    , m_canPatch{ false }
    , m_nameable{ false }
    , m_canStatTrakSwap{ false }
    , m_isCoupon{ false }
    , m_willProduceStatTrak{ false }
{
    // RecursiveParseItem parses the rest
}

PaintKitInfo::PaintKitInfo(const KeyValue &key, float defaultMinFloat, float defaultMaxFloat)
    : m_defIndex{ FromString<uint32_t>(key.Name()) }
    , m_rarity{ ItemSchema::RarityCommon } // rarity is not set here, done in ParsePaintKitRarities
{
    m_minFloat = key.GetNumber<float>("wear_remap_min", defaultMinFloat);
    m_maxFloat = key.GetNumber<float>("wear_remap_max", defaultMaxFloat);
}

StickerKitInfo::StickerKitInfo(const KeyValue &key)
    : m_defIndex{ FromString<uint32_t>(key.Name()) }
    , m_name{ key.GetString("name") }
    , m_rarity{ ItemSchema::RarityDefault }
    , m_tournamentEventId{ 0 }
    , m_tournamentTeamId{ 0 }
    , m_tournamentPlayerId{ 0 }
{
    std::string_view rarity = key.GetString("item_rarity");
    if (rarity.size())
    {
        m_rarity = ItemRarityFromString(rarity);
    }

    m_tournamentEventId = key.GetNumber<uint32_t>("tournament_event_id", 0);
    m_tournamentTeamId = key.GetNumber<uint32_t>("tournament_team_id", 0);
    m_tournamentPlayerId = key.GetNumber<uint32_t>("tournament_player_id", 0);
}

MusicDefinitionInfo::MusicDefinitionInfo(const KeyValue &key)
    : m_defIndex{ FromString<uint32_t>(key.Name()) }
{
    assert(m_defIndex);
}

uint32_t LootListItem::CaseRarity() const
{
    if (quality == ItemSchema::QualityUnusual)
    {
        return ItemSchema::RarityUnusual;
    }

    return rarity;
}

ItemSchema::ItemSchema()
    : ItemSchema{ true }
{
}

ItemSchema::ItemSchema(bool loadFiles)
{
    if (!loadFiles)
    {
        return;
    }

    // read the original schema even if the game's copy was patched for custom case previews
    KeyValue itemSchema{ "root" };
    if (!itemSchema.ParseFromFile(GameSchemaPatch::OriginalSchemaPath()))
    {
        assert(false);
        return;
    }

    const KeyValue *itemsGame = itemSchema.GetSubkey("items_game");
    if (!itemsGame)
    {
        assert(false);
        return;
    }

    const KeyValue *attributesKey = itemsGame->GetSubkey("attributes");
    if (attributesKey)
    {
        ParseAttributes(attributesKey);
    }

    const KeyValue *itemsKey = itemsGame->GetSubkey("items");
    if (itemsKey)
    {
        ParseItems(itemsKey, itemsGame->GetSubkey("prefabs"));
    }

    const KeyValue *seasonalOperationsKey = itemsGame->GetSubkey("seasonaloperations");
    if (seasonalOperationsKey)
    {
        ParseSeasonalOperations(seasonalOperationsKey);
    }

    const KeyValue *stickerKitsKey = itemsGame->GetSubkey("sticker_kits");
    if (stickerKitsKey)
    {
        ParseStickerKits(stickerKitsKey);
    }

    const KeyValue *paintKitsKey = itemsGame->GetSubkey("paint_kits");
    if (paintKitsKey)
    {
        ParsePaintKits(paintKitsKey);
    }

    const KeyValue *paintKitsRarityKey = itemsGame->GetSubkey("paint_kits_rarity");
    if (paintKitsRarityKey)
    {
        ParsePaintKitRarities(paintKitsRarityKey);
    }

    // optional: finishes from mods, usable in custom_cases.txt like any other
    {
        KeyValue customKits{ "custom_paint_kits" };
        if (customKits.ParseFromFile("csgo_gc/custom_paint_kits.txt"))
        {
            ParseCustomPaintKits(customKits);
        }
    }

    const KeyValue *itemSetsKey = itemsGame->GetSubkey("item_sets");
    if (itemSetsKey)
    {
        ParseItemSets(itemSetsKey);
    }

    const KeyValue *musicDefinitionsKey = itemsGame->GetSubkey("music_definitions");
    if (musicDefinitionsKey)
    {
        ParseMusicDefinitions(musicDefinitionsKey);
    }

    // unusual loot lists are not included in client_loot_lists
    // we need to parse these after items and paint kits but before client_loot_lists
    {
        KeyValue unusualLootLists{ "unusual_loot_lists" };

        if (unusualLootLists.ParseFromFile("csgo_gc/unusual_loot_lists.txt"))
        {
            ParseLootLists(&unusualLootLists, true);
        }
        else
        {
            // no knives sorry
            assert(false);
        }
    }

    const KeyValue *lootListsKey = itemsGame->GetSubkey("client_loot_lists");
    if (lootListsKey)
    {
        ParseLootLists(lootListsKey, false);
    }

    // Some loot lists are only present in Valve's GC schema and are not
    // included in the client items_game.txt. Load supplemental definitions
    // from a data file so they can be updated without rebuilding the library.
    {
        KeyValue gcLootLists{ "gc_loot_lists" };

        if (gcLootLists.ParseFromFile("csgo_gc/gc_loot_lists.txt"))
        {
            ParseLootLists(&gcLootLists, false);
        }
        else
        {
            Platform::Print("csgo_gc/gc_loot_lists.txt not found, server-only loot lists will be unavailable\n");
        }
    }

    const KeyValue *revolvingLootListsKey = itemsGame->GetSubkey("revolving_loot_lists");
    if (revolvingLootListsKey)
    {
        ParseRevolvingLootLists(revolvingLootListsKey);
    }

    // optional: replace the contents of existing cases with custom ones
    {
        KeyValue customCases{ "custom_cases" };
        if (customCases.ParseFromFile("csgo_gc/custom_cases.txt"))
        {
            ParseCustomCases(customCases);
        }

        // the client builds a case's contents preview from the loot list the
        // crate points at, so point those lists at the custom contents
        GameSchemaPatch::LootListPreviews previews;
        if (GetConfig().CustomCasePreviews())
        {
            for (const auto &pair : m_customCasePreviewEntries)
            {
                const ItemInfo *crateInfo = ItemInfoByDefIndex(pair.first);
                if (!crateInfo || IsAddedCase(pair.first))
                {
                    continue;
                }

                std::string listName = crateInfo->m_lootListName;
                if (crateInfo->m_supplyCrateSeries && revolvingLootListsKey)
                {
                    listName = std::string{ revolvingLootListsKey->GetString(
                        std::to_string(crateInfo->m_supplyCrateSeries), listName) };
                }

                if (!listName.empty() && !pair.second.empty())
                {
                    previews[listName] = pair.second;
                }
            }
        }

        // the client only offers items for trade-up when a recipe in its
        // items_game.txt matches them, and the game has none for Coverts or golds
        std::string recipes;
        if (GetConfig().GoldTradeUpRecipes())
        {
            recipes = GameSchemaPatch::GoldTradeUpRecipes;
        }

        const bool mixStatTrak = GetConfig().TradeUpMixStatTrak();
        Platform::Print("items_game.txt patch: custom_case_previews=%d (%zu list(s)), gold_tradeup_recipes=%d, tradeup_mix_stattrak=%d\n",
            GetConfig().CustomCasePreviews() ? 1 : 0, previews.size(), GetConfig().GoldTradeUpRecipes() ? 1 : 0,
            mixStatTrak ? 1 : 0);
        // the operation page's goods become the star shop's
        GameSchemaPatch::StarShopPatch starShop;
        if (GetConfig().StarShop().enabled && m_latestSeason)
        {
            starShop.season = *m_latestSeason;
            starShop.blocksText = StarShop::BuildRedeemablesText(
                StarShop::BuildRedeemables(*this, GetConfig().StarShop()));
        }
        starShop.activateOperation = GetConfig().StarShop().operationActive;
        const bool patchStarShop = !starShop.blocksText.empty();

        std::vector<GameSchemaPatch::AddedCasePatch> addedCases;
        for (const AddedCase &added : m_addedCases)
        {
            addedCases.push_back({ added.defIndex, added.basedOn, added.series, added.name, added.previewEntries,
                added.previewRarities, added.hasGolds, added.image });
        }

        GameSchemaPatch::PaintKitsPatch paintKits;
        for (const CustomPaintKit &kit : m_customPaintKits)
        {
            paintKits.kitsText += kit.kitText;
            paintKits.raritiesText += kit.rarityText;
        }

        GameSchemaPatch::LanguageTokens languageTokens;
        for (const CustomPaintKit &kit : m_customPaintKits)
        {
            if (!kit.nameToken.empty())
            {
                languageTokens.emplace_back(kit.nameToken, kit.title);
            }
        }
        if (languageTokens.empty())
        {
            GameSchemaPatch::RestoreLanguage();
        }
        else
        {
            GameSchemaPatch::ApplyLanguage(languageTokens);
        }

        if (!previews.empty() || !recipes.empty() || mixStatTrak || patchStarShop || !addedCases.empty()
            || !paintKits.kitsText.empty())
        {
            GameSchemaPatch::Apply(previews, recipes, mixStatTrak, patchStarShop ? &starShop : nullptr,
                addedCases.empty() ? nullptr : &addedCases, paintKits.kitsText.empty() ? nullptr : &paintKits);
        }
        else
        {
            GameSchemaPatch::Restore();
        }
    }
}

// custom_cases.txt format:
//   "<crate def index>"            the case whose look is reused
//   {
//       "name" "My Case"            optional, shown like a name tag
//       "items" { "[paint_kit]weapon_name" "<rarity name or empty>" ... }
//       "golds" { "[paint_kit]weapon_knife_x" "" ... }
//   }
// Rarity names: common, uncommon, rare, mythical, legendary, ancient. When the
// value isn't a rarity name the paint kit's own rarity is used.
// a crate named crate_* with contents, i.e. a case or package
static bool IsKeyedCrate(const ItemInfo &info)
{
    return !info.m_isCoupon && info.m_name.compare(0, 6, "crate_") == 0
        && (info.m_supplyCrateSeries || !info.m_lootListName.empty());
}

// a case's own inventory picture: csgo/resource/flash/econ/weapon_cases/<case name>.png, loose in the game folder
// (the game reads its pictures from there). returns what items_game.txt's image_inventory should say, or empty
static std::string CaseImageFor(const std::string &caseName)
{
    const std::string file = "csgo/resource/flash/econ/weapon_cases/" + caseName + ".png";
    if (FILE *f = fopen(file.c_str(), "rb"))
    {
        fclose(f);
        return "econ/weapon_cases/" + caseName;
    }
    return {};
}

void ItemSchema::ParseCustomCases(const KeyValue &customCasesKey)
{
    const std::pair<std::string_view, uint32_t> rarityNames[] = {
        { "common", RarityCommon },
        { "uncommon", RarityUncommon },
        { "rare", RarityRare },
        { "mythical", RarityMythical },
        { "legendary", RarityLegendary },
        { "ancient", RarityAncient },
    };

    uint32_t currentCrateDefIndex = 0;
    auto parseItems = [&](const KeyValue *itemsKey, LootList &list, bool golds)
    {
        if (!itemsKey)
        {
            return;
        }

        for (const KeyValue &entry : *itemsKey)
        {
            // validate "[paint_kit]weapon" first: ParseLootListItem asserts on unknown paint kits
            std::string_view name = entry.Name();
            size_t close = name.find(']');
            bool known = name.size() > 2 && name[0] == '[' && close != std::string_view::npos
                && PaintKitInfoByName(name.substr(1, close - 1))
                && ItemInfoByName(name.substr(close + 1));

            LootListItem item;
            if (!known || !ParseLootListItem(item, name))
            {
                Platform::Print("custom_cases: skipping unknown item %s\n", std::string{ entry.Name() }.c_str());
                continue;
            }

            if (golds)
            {
                item.quality = QualityUnusual;
            }
            else
            {
                for (const auto &pair : rarityNames)
                {
                    if (pair.first == entry.String())
                    {
                        item.rarity = pair.second;
                    }
                }
            }

            if (item.rarity < RarityCommon || item.rarity > RarityImmortal)
            {
                item.rarity = golds ? RarityAncient : RarityCommon;
            }

            list.items.push_back(item);
            if (!golds)
            {
                m_customCasePreviewEntries[currentCrateDefIndex].emplace_back(name);
                m_customCasePreviewRarities[currentCrateDefIndex].push_back(item.rarity);
            }
        }
    };

    // a key that isn't a number adds a new case. its def index comes from the
    // key's name, so it is the same on every run and saved items keep working
    auto isNumber = [](std::string_view text)
    {
        return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    auto addedDefIndex = [&](std::string_view key)
    {
        uint32_t hash = 2166136261u;
        for (char c : key)
        {
            hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
        }

        uint32_t defIndex = 20000 + hash % 30000;
        while (m_itemInfo.contains(defIndex))
        {
            defIndex = defIndex >= 49999 ? 20000 : defIndex + 1;
        }
        return defIndex;
    };

    for (const KeyValue &caseKey : customCasesKey)
    {
        uint32_t crateDefIndex = 0;
        uint32_t addedBasedOn = 0;
        std::string addedName;
        if (isNumber(caseKey.Name()))
        {
            crateDefIndex = FromString<uint32_t>(caseKey.Name());
        }
        else
        {
            addedBasedOn = caseKey.GetNumber<uint32_t>("based_on");
            const ItemInfo *basedOn = ItemInfoByDefIndex(addedBasedOn);
            if (!basedOn || !IsKeyedCrate(*basedOn))
            {
                Platform::Print("custom_cases: %s needs \"based_on\" with the def index of an existing case\n",
                    std::string{ caseKey.Name() }.c_str());
                continue;
            }

            addedName = "crate_csgogc_";
            for (char c : caseKey.Name())
            {
                addedName += (isalnum(static_cast<unsigned char>(c))) ? static_cast<char>(tolower(c)) : '_';
            }
            if (m_itemDefIndexByName.contains(addedName))
            {
                Platform::Print("custom_cases: another case is already called %s\n", addedName.c_str());
                continue;
            }

            crateDefIndex = addedDefIndex(caseKey.Name());
            ItemInfo added = *basedOn;
            added.m_defIndex = crateDefIndex;
            added.m_name = addedName;
            added.m_supplyCrateSeries = crateDefIndex; // unique, so it gets its own contents list
            m_itemInfo.emplace(crateDefIndex, std::move(added));
            m_itemDefIndexByName.emplace(addedName, crateDefIndex);
        }

        currentCrateDefIndex = crateDefIndex;
        const ItemInfo *crateInfo = ItemInfoByDefIndex(crateDefIndex);
        if (!crateInfo)
        {
            Platform::Print("custom_cases: no item with def index %s\n", std::string{ caseKey.Name() }.c_str());
            continue;
        }

        const std::string baseName = "custom_case_" + std::to_string(crateDefIndex);

        LootList &regular = m_lootLists[baseName + "_items"];
        regular = LootList{};
        parseItems(caseKey.GetSubkey("items"), regular, false);

        LootList &golds = m_lootLists[baseName + "_golds"];
        golds = LootList{};
        golds.isUnusual = true;
        parseItems(caseKey.GetSubkey("golds"), golds, true);

        if (regular.items.empty() && golds.items.empty())
        {
            m_customCasePreviewEntries.erase(crateDefIndex);
            m_customCasePreviewRarities.erase(crateDefIndex);
            Platform::Print("custom_cases: case %u has no valid items, ignoring\n", crateDefIndex);
            if (addedBasedOn)
            {
                m_itemDefIndexByName.erase(addedName);
                m_itemInfo.erase(crateDefIndex);
            }
            continue;
        }

        // the case opener expects a list to have either items or sublists
        LootList &root = m_lootLists[baseName];
        root = LootList{};
        if (!regular.items.empty())
        {
            root.subLists.push_back(&regular);
        }
        if (!golds.items.empty())
        {
            root.subLists.push_back(&golds);
        }

        m_customCaseLootLists[crateDefIndex] = &root;
        if (addedBasedOn)
        {
            m_addedCases.push_back({ crateDefIndex, addedBasedOn, crateDefIndex, addedName,
                m_customCasePreviewEntries[crateDefIndex], m_customCasePreviewRarities[crateDefIndex],
                !golds.items.empty(), CaseImageFor(addedName) });
        }

        std::string_view customName = caseKey.GetString("name");
        if (!customName.empty())
        {
            m_customCaseNames[crateDefIndex] = std::string{ customName };
        }

        if (const uint32_t price = caseKey.GetNumber<uint32_t>("price", 0))
        {
            m_customCasePrices[crateDefIndex] = price;
        }

        // the case is its own collection for trade-ups. "zz_" sorts it after
        // the real collections, which win when they know the same skin
        {
            const std::string setName = "zz_csgogc_case_" + std::to_string(crateDefIndex);
            ItemSet set;
            set.name = customName.empty() ? crateInfo->m_name : std::string{ customName };
            set.isCollection = true;
            set.isCustomCase = true;
            for (const LootListItem &item : regular.items)
            {
                if (item.type == LootListItemPaintable && item.itemInfo && item.paintKitInfo)
                {
                    set.items.push_back(item);
                }
            }

            if (!set.items.empty() || !golds.items.empty())
            {
                m_itemSets[setName] = std::move(set);
                if (!golds.items.empty())
                {
                    m_customCaseGoldLists[setName] = &golds;
                }
            }
        }
        Platform::Print("custom_cases: %s (%u) now has %zu skins and %zu golds\n",
            crateInfo->m_name.c_str(), crateDefIndex, regular.items.size(), golds.items.size());
    }
}

static std::string DecodeAttributeString(std::string_view data)
{
    CAttribute_String attribute;
    if (!attribute.ParseFromString(data))
    {
        // Inventories written before string attributes used the protobuf
        // wrapper may contain the raw value. Keep those inventories readable.
        return std::string{ data };
    }

    return attribute.value();
}

static std::string EncodeAttributeString(std::string_view string)
{
    CAttribute_String attribute;
    attribute.set_value(string);
    return attribute.SerializeAsString();
}

float ItemSchema::AttributeFloat(const CSOEconItemAttribute *attribute) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return 0;
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
        return *reinterpret_cast<const float *>(attribute->value_bytes().data());

    case AttributeType::Uint32:
        return *reinterpret_cast<const uint32_t *>(attribute->value_bytes().data());

    case AttributeType::String:
        return FromString<float>(DecodeAttributeString(attribute->value_bytes()));

    default:
        assert(false);
        return 0;
    }
}

uint32_t ItemSchema::AttributeUint32(const CSOEconItemAttribute *attribute) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return 0;
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
        return *reinterpret_cast<const float *>(attribute->value_bytes().data());

    case AttributeType::Uint32:
        return *reinterpret_cast<const uint32_t *>(attribute->value_bytes().data());

    case AttributeType::String:
        return FromString<uint32_t>(DecodeAttributeString(attribute->value_bytes()));

    default:
        assert(false);
        return 0;
    }
}

std::string ItemSchema::AttributeString(const CSOEconItemAttribute *attribute) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return {};
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
        return std::to_string(*reinterpret_cast<const float *>(attribute->value_bytes().data()));

    case AttributeType::Uint32:
        return std::to_string(*reinterpret_cast<const uint32_t *>(attribute->value_bytes().data()));

    case AttributeType::String:
        return DecodeAttributeString(attribute->value_bytes());

    default:
        assert(false);
        return {};
    }
}

bool ItemSchema::SetAttributeFloat(CSOEconItemAttribute *attribute, float value) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return false;
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
    {
        attribute->set_value_bytes(&value, sizeof(value));
        break;
    }

    case AttributeType::Uint32:
    {
        uint32_t convert = static_cast<uint32_t>(value);
        attribute->set_value_bytes(&convert, sizeof(convert));
        break;
    }

    case AttributeType::String:
    {
        std::string convert = std::to_string(value);
        attribute->set_value_bytes(EncodeAttributeString(convert));
        break;
    }

    default:
        assert(false);
        return false;
    }

    return true;
}

bool ItemSchema::SetAttributeUint32(CSOEconItemAttribute *attribute, uint32_t value) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return false;
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
    {
        float convert = static_cast<float>(value);
        attribute->set_value_bytes(&convert, sizeof(convert));
        break;
    }

    case AttributeType::Uint32:
    {
        attribute->set_value_bytes(&value, sizeof(value));
        break;
    }

    case AttributeType::String:
    {
        std::string convert = std::to_string(value);
        attribute->set_value_bytes(EncodeAttributeString(convert));
        break;
    }

    default:
        assert(false);
        return false;
    }

    return true;
}

bool ItemSchema::SetAttributeString(CSOEconItemAttribute *attribute, std::string_view value) const
{
    auto it = m_attributeInfo.find(attribute->def_index());
    if (it == m_attributeInfo.end())
    {
        assert(false);
        return false;
    }

    switch (it->second.m_type)
    {
    case AttributeType::Float:
    {
        float convert = FromString<float>(value);
        attribute->set_value_bytes(&convert, sizeof(convert));
        break;
    }

    case AttributeType::Uint32:
    {
        uint32_t convert = FromString<uint32_t>(value);
        attribute->set_value_bytes(&convert, sizeof(convert));
        break;
    }

    case AttributeType::String:
    {
        attribute->set_value_bytes(EncodeAttributeString(value));
        break;
    }

    default:
        assert(false);
        return false;
    }

    return true;
}

const LootList *ItemSchema::GetCrateLootList(uint32_t crateDefIndex) const
{
    auto customSearch = m_customCaseLootLists.find(crateDefIndex);
    if (customSearch != m_customCaseLootLists.end())
    {
        return customSearch->second;
    }

    return GetOriginalCrateLootList(crateDefIndex);
}

const LootList *ItemSchema::GetOriginalCrateLootList(uint32_t crateDefIndex) const
{
    auto itemSearch = m_itemInfo.find(crateDefIndex);
    if (itemSearch == m_itemInfo.end())
    {
        Platform::Print("GetCrateLootList: crate def_index %u not found\n", crateDefIndex);
        return nullptr;
    }

    const ItemInfo &itemInfo = itemSearch->second;

    if (itemInfo.m_supplyCrateSeries)
    {
        auto lootListSearch = m_revolvingLootLists.find(itemInfo.m_supplyCrateSeries);
        if (lootListSearch != m_revolvingLootLists.end())
        {
            return &lootListSearch->second;
        }
    }

    // Self-opening collection packages, agent containers and tournament
    // capsules point directly at a loot list instead of a revolving series.
    // Coupon definitions also use loot_list_name, but their list describes
    // the purchased item rather than contents that can be opened.
    if (!itemInfo.m_isCoupon && !itemInfo.m_lootListName.empty())
    {
        auto lootListSearch = m_lootLists.find(itemInfo.m_lootListName);
        if (lootListSearch != m_lootLists.end())
        {
            return &lootListSearch->second;
        }
    }

    return nullptr;
}

static bool LootListContainsItemType(const LootList &lootList, LootListItemType type)
{
    for (const LootListItem &item : lootList.items)
    {
        if (item.type == type)
        {
            return true;
        }
    }

    for (const LootList *subList : lootList.subLists)
    {
        if (subList && LootListContainsItemType(*subList, type))
        {
            return true;
        }
    }

    return false;
}

bool ItemSchema::IsSouvenirPackage(const CSOEconItem &item) const
{
    const ItemInfo *itemInfo = ItemInfoByDefIndex(item.def_index());
    if (!itemInfo)
    {
        return false;
    }

    const LootList *lootList = GetCrateLootList(item.def_index());
    if (!lootList || !LootListContainsItemType(*lootList, LootListItemPaintable))
    {
        // Tournament sticker capsules can carry event metadata too, but their
        // loot lists contain stickers rather than painted souvenir weapons.
        return false;
    }

    // The common souvenir package prefabs identify both legacy and modern
    // packages without relying on a broad def_index range.
    for (const std::string &prefab : itemInfo->m_prefabs)
    {
        if (prefab.find("souvenir") != std::string::npos)
        {
            return true;
        }
    }

    uint32_t tournamentEventId = itemInfo->m_tournament.eventId;
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        if (attribute.def_index() == AttributeTournamentEventId)
        {
            tournamentEventId = AttributeUint32(&attribute);
            break;
        }
    }

    return tournamentEventId != 0;
}

bool ItemSchema::CreateItemFromLootListItem(Random &random,
    const LootListItem &lootListItem,
    bool statTrak,
    ItemOrigin origin,
    UnacknowledgedType unacknowledgedType,
    CSOEconItem &item) const
{
    if (!CreateItem(lootListItem.itemInfo->m_defIndex, origin, unacknowledgedType, item))
    {
        assert(false);
        return false;
    }

    // Painted weapons are normal schema items, but their econ instances are unique
    // unless StatTrak elevates them to strange. The client-side trade-up filters
    // key off this quality before the GC ever sees a craft request.
    if (statTrak && lootListItem.quality != ItemSchema::QualityUnusual)
    {
        item.set_quality(ItemSchema::QualityStrange);
    }
    else if (lootListItem.type == LootListItemPaintable && lootListItem.quality != ItemSchema::QualityUnusual)
    {
        item.set_quality(ItemSchema::QualityUnique);
    }
    else
    {
        item.set_quality(lootListItem.quality);
    }

    // rarity override
    assert(lootListItem.rarity >= ItemSchema::RarityCommon && lootListItem.rarity <= ItemSchema::RarityImmortal);
    item.set_rarity(lootListItem.rarity);

    // setup type specficic attributes

    if (lootListItem.type == LootListItemSticker)
    {
        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeStickerId0);
        SetAttributeUint32(attribute, lootListItem.stickerKitInfo->m_defIndex);

        // Tournament capsules produce normal sticker items that retain their
        // event metadata. This is distinct from souvenir weapon generation.
        if (lootListItem.stickerKitInfo->m_tournamentEventId)
        {
            attribute = item.add_attribute();
            attribute->set_def_index(ItemSchema::AttributeTournamentEventId);
            SetAttributeUint32(attribute, lootListItem.stickerKitInfo->m_tournamentEventId);
        }
    }
    else if (lootListItem.type == LootListItemSpray)
    {
        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeStickerId0);
        SetAttributeUint32(attribute, lootListItem.stickerKitInfo->m_defIndex);

        // add AttributeSpraysRemaining when it's unsealed (mikkotodo how does the real gc do this)

        attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeSprayTintId);
        SetAttributeUint32(attribute, random.Integer<uint32_t>(ItemSchema::GraffitiTintMin, ItemSchema::GraffitiTintMax));
    }
    else if (lootListItem.type == LootListItemPatch)
    {
        // mikkotodo anything else?
        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeStickerId0);
        SetAttributeUint32(attribute, lootListItem.stickerKitInfo->m_defIndex);
    }
    else if (lootListItem.type == LootListItemMusicKit)
    {
        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeMusicId);
        SetAttributeUint32(attribute, lootListItem.musicDefinitionInfo->m_defIndex);
    }
    else if (lootListItem.type == LootListItemPaintable)
    {
        const PaintKitInfo *paintKitInfo = lootListItem.paintKitInfo;

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeTexturePrefab);
        SetAttributeUint32(attribute, paintKitInfo->m_defIndex);

        attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeTextureSeed);
        SetAttributeUint32(attribute, random.Integer<uint32_t>(0, 1000));

        // mikkotodo how does the float distribution work?
        attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeTextureWear);
        SetAttributeFloat(attribute, random.Float(paintKitInfo->m_minFloat, paintKitInfo->m_maxFloat));
    }
    else if (lootListItem.type == LootListItemNoAttribute)
    {
        // nothing
    }
    else
    {
        assert(false);
    }

    if (statTrak)
    {
        assert((lootListItem.type == LootListItemMusicKit) || (lootListItem.type == LootListItemPaintable));

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeKillEater);
        SetAttributeUint32(attribute, 0);

        // score type: 1 for music kits, 0 for weapons
        int scoreType = (lootListItem.type == LootListItemMusicKit) ? 1 : 0;

        attribute = item.add_attribute();
        attribute->set_def_index(ItemSchema::AttributeKillEaterScoreType);
        SetAttributeUint32(attribute, scoreType);
    }

    return true;
}

bool ItemSchema::CanCreateItem(uint32_t defIndex) const
{
    auto itemSearch = m_itemInfo.find(defIndex);
    if (itemSearch == m_itemInfo.end())
    {
        return false;
    }

    const ItemInfo &itemInfo = itemSearch->second;
    if (!itemInfo.m_isCoupon)
    {
        return true;
    }

    auto lootListSearch = m_lootLists.find(itemInfo.m_lootListName);
    if (lootListSearch == m_lootLists.end())
    {
        return false;
    }

    const LootList &lootList = lootListSearch->second;
    if (!lootList.subLists.empty() || lootList.items.size() != 1
        || lootList.willProduceStatTrak || lootList.isUnusual)
    {
        return false;
    }

    const LootListItem &lootListItem = lootList.items.front();
    return lootListItem.itemInfo
        && !lootListItem.itemInfo->m_isCoupon
        && (!itemInfo.m_willProduceStatTrak
            || lootListItem.type == LootListItemMusicKit
            || lootListItem.type == LootListItemPaintable);
}

bool ItemSchema::CreateItem(uint32_t defIndex, ItemOrigin origin, UnacknowledgedType unacknowledgedType, CSOEconItem &econItem) const
{
    auto itemSearch = m_itemInfo.find(defIndex);
    if (itemSearch == m_itemInfo.end())
    {
        assert(false);
        return false;
    }

    const ItemInfo &itemInfo = itemSearch->second;

    // urgh wtf is this crap
    if (itemInfo.m_isCoupon)
    {
        if (!CanCreateItem(defIndex))
        {
            assert(false);
            return false;
        }

        auto lootListSearch = m_lootLists.find(itemInfo.m_lootListName);
        const LootList &lootList = lootListSearch->second;

        Random random;

        return CreateItemFromLootListItem(random,
            lootList.items.front(),
            itemInfo.m_willProduceStatTrak,
            origin,
            unacknowledgedType,
            econItem);
    }

    econItem.set_inventory(InventoryUnacknowledged(unacknowledgedType));
    econItem.set_def_index(defIndex);
    econItem.set_quantity(1);
    econItem.set_level(itemInfo.m_level);
    econItem.set_quality(itemInfo.m_quality);
    econItem.set_flags(0);
    econItem.set_origin(origin);
    econItem.set_in_use(false);
    econItem.set_rarity(itemInfo.m_rarity);

    if (!ApplyGeneratedAttributes(itemInfo, econItem))
    {
        return false;
    }

    // custom cases can carry a display name, shown like a name tag
    if (const std::string *customName = CustomCaseName(defIndex))
    {
        CSOEconItemAttribute *nameAttribute = econItem.add_attribute();
        nameAttribute->set_def_index(AttributeCustomName);
        SetAttributeString(nameAttribute, *customName);
    }

    return true;
}

std::vector<uint32_t> ItemSchema::GetCustomCaseDefIndexes() const
{
    std::vector<uint32_t> cases;
    for (const auto &pair : m_customCaseLootLists)
    {
        cases.push_back(pair.first);
    }
    std::sort(cases.begin(), cases.end());
    return cases;
}

bool ItemSchema::IsAddedCase(uint32_t crateDefIndex) const
{
    return std::any_of(m_addedCases.begin(), m_addedCases.end(),
        [&](const AddedCase &added) { return added.defIndex == crateDefIndex; });
}

bool ItemSchema::IsCustomCase(uint32_t crateDefIndex) const
{
    return m_customCaseLootLists.contains(crateDefIndex);
}

uint32_t ItemSchema::CustomCasePrice(uint32_t crateDefIndex) const
{
    auto it = m_customCasePrices.find(crateDefIndex);
    return it != m_customCasePrices.end() ? it->second : 0;
}

const std::string *ItemSchema::CustomCaseName(uint32_t crateDefIndex) const
{
    auto it = m_customCaseNames.find(crateDefIndex);
    return it != m_customCaseNames.end() ? &it->second : nullptr;
}

bool ItemSchema::ApplyGeneratedAttributes(const ItemInfo &info, CSOEconItem &item) const
{
    for (const GeneratedItemAttribute &generated : info.m_generatedAttributes)
    {
        auto attributeInfo = m_attributeInfo.find(generated.defIndex);
        if (attributeInfo == m_attributeInfo.end())
        {
            return false;
        }

        CSOEconItemAttribute *attribute = item.add_attribute();
        attribute->set_def_index(generated.defIndex);

        bool applied = false;
        switch (attributeInfo->second.m_type)
        {
        case AttributeType::Float:
            applied = SetAttributeFloat(attribute, FromString<float>(generated.value));
            break;
        case AttributeType::Uint32:
            applied = SetAttributeUint32(attribute, FromString<uint32_t>(generated.value));
            break;
        case AttributeType::String:
            applied = SetAttributeString(attribute, generated.value);
            break;
        }

        if (!applied)
        {
            return false;
        }
    }

    return true;
}

void ItemSchema::ParseItems(const KeyValue *itemsKey, const KeyValue *prefabsKey)
{
    m_itemInfo.reserve(itemsKey->SubkeyCount());

    for (const KeyValue &itemKey : *itemsKey)
    {
        if (itemKey.Name() == "default")
        {
            // ignore this
            continue;
        }

        uint32_t defIndex = FromString<uint32_t>(itemKey.Name());
        auto emplace = m_itemInfo.try_emplace(defIndex, defIndex);

        ParseItemRecursive(emplace.first->second, itemKey, prefabsKey);

        auto &itemInfo = emplace.first->second;

        if (!itemInfo.m_name.empty())
        {
            m_itemDefIndexByName[itemInfo.m_name] = defIndex;
        }

        if (itemInfo.m_seasonAccess && itemInfo.m_level == 1
            && std::find(itemInfo.m_prefabs.begin(), itemInfo.m_prefabs.end(), "operation_coin")
                != itemInfo.m_prefabs.end())
        {
            m_operationCoinDefIndexBySeason.try_emplace(*itemInfo.m_seasonAccess, defIndex);
        }

        // FIXME: remove, temp slop to make sure we parse correctly
        if (itemInfo.m_isCoupon)
        {
            assert(itemInfo.m_lootListName.size());
        }
        else
        {
            assert(!itemInfo.m_willProduceStatTrak);
        }
    }
}

// ideally this would get parsed from the item schema...
static uint32_t ItemQualityFromString(std::string_view name)
{
    const std::pair<std::string_view, uint32_t> qualityNames[] = {
        { "normal", ItemSchema::QualityNormal },
        { "genuine", ItemSchema::QualityGenuine },
        { "vintage", ItemSchema::QualityVintage },
        { "unusual", ItemSchema::QualityUnusual },
        { "unique", ItemSchema::QualityUnique },
        { "community", ItemSchema::QualityCommunity },
        { "developer", ItemSchema::QualityDeveloper },
        { "selfmade", ItemSchema::QualitySelfmade },
        { "customized", ItemSchema::QualityCustomized },
        { "strange", ItemSchema::QualityStrange },
        { "completed", ItemSchema::QualityCompleted },
        { "haunted", ItemSchema::QualityHaunted },
        { "tournament", ItemSchema::QualityTournament },
    };

    for (const auto &pair : qualityNames)
    {
        if (pair.first == name)
        {
            return pair.second;
        }
    }

    assert(false);
    return ItemSchema::QualityUnique; // i guess???
}

// i hate my life
static std::vector<std::string_view> SplitString(std::string_view input, char delimiter)
{
    size_t offset = 0;
    std::vector<std::string_view> result;

    while (true)
    {
        size_t i = input.find(delimiter, offset);
        if (i == std::string_view::npos)
        {
            result.emplace_back(input.substr(offset));
            break;
        }

        result.emplace_back(input.substr(offset, i - offset));
        offset = i + 1;
    }

    return result;
}

void ItemSchema::ParseItemRecursive(ItemInfo &info, const KeyValue &itemKey, const KeyValue *prefabsKey)
{
    std::string_view prefabString = itemKey.GetString("prefab");
    if (prefabString.size() && prefabsKey)
    {
        // might have multiple specifications in a single statement
        std::vector<std::string_view> prefabNames = SplitString(prefabString, ' ');
        for (std::string_view prefabName : prefabNames)
        {
            info.m_prefabs.emplace_back(prefabName);

            const KeyValue *prefabKey = prefabsKey->GetSubkey(prefabName);
            if (prefabKey)
            {
                ParseItemRecursive(info, *prefabKey, prefabsKey);
            }
            else
            {
                // not available to us mortals...
                Platform::Print("No such prefab '%s'\n", std::string{ prefabName }.c_str());
            }
        }
    }

    std::string_view name = itemKey.GetString("name");
    if (name.size())
    {
        info.m_name = name;
    }

    std::string_view quality = itemKey.GetString("item_quality");
    if (quality.size())
    {
        info.m_quality = ItemQualityFromString(quality);
    }

    std::string_view rarity = itemKey.GetString("item_rarity");
    if (rarity.size())
    {
        info.m_rarity = ItemRarityFromString(rarity);
    }

    uint32_t minLevel = itemKey.GetNumber<uint32_t>("min_ilevel", 0);
    uint32_t maxLevel = itemKey.GetNumber<uint32_t>("max_ilevel", 0);
    if (minLevel && maxLevel)
    {
        assert(minLevel == maxLevel);
        info.m_level = minLevel;
    }

    std::string_view itemType = itemKey.GetString("item_type");
    if (itemType.size())
    {
        info.m_itemType = itemType;
        info.m_isCoupon = (itemType == "coupon");
    }

    std::string_view lootListName = itemKey.GetString("loot_list_name");
    if (lootListName.size())
    {
        info.m_lootListName = lootListName;
    }

    const KeyValue *stickers = itemKey.GetSubkey("stickers");
    if (stickers)
    {
        for (const KeyValue &sticker : *stickers)
        {
            uint32_t slot = FromString<uint32_t>(sticker.Name());
            if (slot >= static_cast<uint32_t>(MaxStickers))
            {
                Platform::Print("Item %u has unsupported sticker slot %u\n", info.m_defIndex, slot);
                continue;
            }

            info.m_stickerSlotCount = std::max(info.m_stickerSlotCount, slot + 1);
        }
    }

    info.m_willProduceStatTrak = itemKey.GetNumber("will_produce_stattrak", false);

    const KeyValue *capabilities = itemKey.GetSubkey("capabilities");
    if (capabilities)
    {
        const KeyValue *canSticker = capabilities->GetSubkey("can_sticker");
        if (canSticker)
        {
            info.m_canSticker = FromString<int>(canSticker->String()) != 0;
        }

        const KeyValue *canPatch = capabilities->GetSubkey("can_patch");
        if (canPatch)
        {
            info.m_canPatch = FromString<int>(canPatch->String()) != 0;
        }

        const KeyValue *nameable = capabilities->GetSubkey("nameable");
        if (nameable)
        {
            info.m_nameable = FromString<int>(nameable->String()) != 0;
        }

        const KeyValue *canStatTrakSwap = capabilities->GetSubkey("can_stattrack_swap");
        if (canStatTrakSwap)
        {
            info.m_canStatTrakSwap = FromString<int>(canStatTrakSwap->String()) != 0;
        }
    }

    const KeyValue *tool = itemKey.GetSubkey("tool");
    if (tool)
    {
        std::string_view type = tool->GetString("type");
        if (type.size())
        {
            info.m_toolType = type;
        }

        std::string_view restriction = tool->GetString("restriction");
        if (restriction.size())
        {
            info.m_toolRestriction = restriction;
        }
    }

    const KeyValue *attributes = itemKey.GetSubkey("attributes");
    if (attributes)
    {
        const KeyValue *seasonAccess = attributes->GetSubkey("season access");
        if (seasonAccess)
        {
            info.m_seasonAccess = FromString<uint32_t>(seasonAccess->String());
        }

        info.m_prestigeYear = attributes->GetNumber<uint32_t>(
            "prestige year", info.m_prestigeYear);

        const KeyValue *supplyCrateSeries = attributes->GetSubkey("set supply crate series");
        if (supplyCrateSeries)
        {
            info.m_supplyCrateSeries = supplyCrateSeries->GetNumber<uint32_t>("value");
        }

        auto parseTournamentAttribute = [&](std::string_view name, uint32_t &value)
        {
            const KeyValue *attribute = attributes->GetSubkey(name);
            if (attribute)
            {
                value = attribute->GetNumber<uint32_t>("value");
            }
        };

        parseTournamentAttribute("tournament event id", info.m_tournament.eventId);
        parseTournamentAttribute("tournament event stage id", info.m_tournament.stageId);
        parseTournamentAttribute("tournament event team0 id", info.m_tournament.team0Id);
        parseTournamentAttribute("tournament event team1 id", info.m_tournament.team1Id);
        parseTournamentAttribute("tournament mvp account id", info.m_tournament.mvpAccountId);

        for (const KeyValue &attribute : *attributes)
        {
            if (!attribute.GetNumber<int>("force_gc_to_generate"))
            {
                continue;
            }

            auto defIndex = m_attributeDefIndexByName.find(std::string{ attribute.Name() });
            if (defIndex == m_attributeDefIndexByName.end())
            {
                Platform::Print("Unknown generated attribute '%s' for item %u\n",
                    std::string{ attribute.Name() }.c_str(), info.m_defIndex);
                continue;
            }

            const std::string value{ attribute.GetString("value") };
            auto existing = std::find_if(info.m_generatedAttributes.begin(),
                info.m_generatedAttributes.end(), [&](const GeneratedItemAttribute &generated) {
                    return generated.defIndex == defIndex->second;
                });
            if (existing != info.m_generatedAttributes.end())
            {
                existing->value = value;
            }
            else
            {
                info.m_generatedAttributes.push_back({ defIndex->second, value });
            }
        }
    }
}

void ItemSchema::ParseSeasonalOperations(const KeyValue *seasonalOperationsKey)
{
    for (const KeyValue &operationKey : *seasonalOperationsKey)
    {
        const uint32_t seasonValue = FromString<uint32_t>(operationKey.Name());
        if (!m_latestSeason || seasonValue > *m_latestSeason)
        {
            m_latestSeason = seasonValue;
        }

        for (const KeyValue &entry : operationKey)
        {
            if (entry.Name() != "quest_mission_card")
            {
                continue;
            }

            const uint32_t missionCardId = entry.GetNumber<uint32_t>("id");
            if (!missionCardId)
            {
                continue;
            }

            std::vector<uint32_t> &missionCards = m_missionCardsBySeason[seasonValue];
            if (std::find(missionCards.begin(), missionCards.end(), missionCardId)
                == missionCards.end())
            {
                missionCards.push_back(missionCardId);
            }
        }
    }
}

void ItemSchema::ParseAttributes(const KeyValue *attributesKey)
{
    m_attributeInfo.reserve(attributesKey->SubkeyCount());

    for (const KeyValue &attributeKey : *attributesKey)
    {
        uint32_t defIndex = FromString<uint32_t>(attributeKey.Name());
        assert(defIndex);
        m_attributeInfo.try_emplace(defIndex, attributeKey);

        std::string_view name = attributeKey.GetString("name");
        if (!name.empty())
        {
            m_attributeDefIndexByName[std::string{ name }] = defIndex;
        }
    }
}

void ItemSchema::ParseStickerKits(const KeyValue *stickerKitsKey)
{
    m_stickerKitInfo.reserve(stickerKitsKey->SubkeyCount());

    for (const KeyValue &stickerKitKey : *stickerKitsKey)
    {
        std::string_view name = stickerKitKey.GetString("name");

        m_stickerKitInfo.emplace(std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(stickerKitKey));
    }
}

void ItemSchema::ParsePaintKits(const KeyValue *paintKitsKey)
{
    m_paintKitInfo.reserve(paintKitsKey->SubkeyCount());

    float defaultMinFloat = 0.0f;
    float defaultMaxFloat = 1.0f;
    if (const KeyValue *defaultPaintKitKey = paintKitsKey->GetSubkey("0"))
    {
        defaultMinFloat = defaultPaintKitKey->GetNumber<float>("wear_remap_min", defaultMinFloat);
        defaultMaxFloat = defaultPaintKitKey->GetNumber<float>("wear_remap_max", defaultMaxFloat);
    }

    for (const KeyValue &paintKitKey : *paintKitsKey)
    {
        std::string_view name = paintKitKey.GetString("name");

        m_paintKitInfo.emplace(std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(paintKitKey, defaultMinFloat, defaultMaxFloat));
    }
}

// writes a KeyValue's children as items_game.txt text
static void WriteKitChildren(std::string &out, const KeyValue &key, int indent)
{
    const std::string tabs(indent, '\t');
    for (const KeyValue &child : key)
    {
        if (child.SubkeyCount())
        {
            out += tabs + "\"" + std::string{ child.Name() } + "\"\n" + tabs + "{\n";
            WriteKitChildren(out, child, indent + 1);
            out += tabs + "}\n";
        }
        else
        {
            out += tabs + "\"" + std::string{ child.Name() } + "\"\t\t\"" + std::string{ child.String() } + "\"\n";
        }
    }
}

// custom_paint_kits.txt format: one block per finish, named after the finish
//   "<name>"
//   {
//       "id"     "11001"    optional, picked from the name when missing
//       "title"  "Jaguar Purple"   optional display name
//       "rarity" "ancient"  optional, ancient when missing
//       anything else is copied into items_game.txt's paint_kits entry, e.g.
//       "vmt_path" "materials/models/weapons/customization/paints_gloves/x.vmt"
//       or "style", "pattern", "color0" ... for a finish painted on weapons
//   }
void ItemSchema::ParseCustomPaintKits(const KeyValue &customKitsKey)
{
    for (const KeyValue &kitKey : customKitsKey)
    {
        const std::string name{ kitKey.Name() };
        if (name.empty() || name.find('"') != std::string::npos || m_paintKitInfo.contains(name))
        {
            Platform::Print("custom_paint_kits: skipping %s (empty, has a quote, or the game already has it)\n",
                name.c_str());
            continue;
        }

        uint32_t defIndex = kitKey.GetNumber<uint32_t>("id", 0);
        if (!defIndex)
        {
            uint32_t hash = 2166136261u;
            for (char c : name)
            {
                hash = (hash ^ static_cast<uint8_t>(c)) * 16777619u;
            }
            defIndex = 11000 + hash % 900;
        }

        const auto taken = [&](uint32_t index)
        {
            for (const auto &pair : m_paintKitInfo)
            {
                if (pair.second.m_defIndex == index)
                {
                    return true;
                }
            }
            return false;
        };
        const bool explicitId = kitKey.GetSubkey("id") != nullptr;
        if (explicitId && taken(defIndex))
        {
            Platform::Print("custom_paint_kits: %s wants id %u, which the game already uses\n", name.c_str(), defIndex);
            continue;
        }
        while (taken(defIndex))
        {
            defIndex++;
        }

        std::string title{ kitKey.GetString("title") };
        if (title.empty())
        {
            title = name;
            for (char &c : title)
            {
                if (c == '_')
                {
                    c = ' ';
                }
            }
        }
        if (title.find('"') != std::string::npos)
        {
            title.clear();
            title = name;
        }

        const std::string rarityName{ kitKey.GetString("rarity", "ancient") };

        // the client shows a finish's name from a language token; with the game's language file
        // extracted next to it, that token is added to it. without it the plain title is all there is
        std::string nameToken;
        if (GameSchemaPatch::LanguageFileAvailable())
        {
            nameToken = "PaintKit_csgogc_" + name + "_Tag";
        }

        // the entry the schema and the game file share
        KeyValue entry{ std::to_string(defIndex) };
        entry.AddString("name", name);
        entry.AddString("description_string", title);
        entry.AddString("description_tag", nameToken.empty() ? title : "#" + nameToken);
        for (const KeyValue &child : kitKey)
        {
            const std::string_view childName = child.Name();
            if (childName == "id" || childName == "title" || childName == "rarity" || childName == "name"
                || childName == "description_string" || childName == "description_tag")
            {
                continue;
            }
            entry.AddSubkey(childName) = child;
        }

        auto emplaced = m_paintKitInfo.emplace(std::piecewise_construct, std::forward_as_tuple(name),
            std::forward_as_tuple(entry, 0.0f, 1.0f));
        emplaced.first->second.m_rarity = ItemRarityFromString(rarityName);

        CustomPaintKit custom;
        custom.defIndex = defIndex;
        custom.name = name;
        custom.title = title;
        custom.nameToken = nameToken;
        custom.kitText = "\t\t\"" + std::to_string(defIndex) + "\"\n\t\t{\n";
        WriteKitChildren(custom.kitText, entry, 3);
        custom.kitText += "\t\t}\n";
        custom.rarityText = "\t\t\"" + name + "\"\t\t\"" + rarityName + "\"\n";
        m_customPaintKits.push_back(std::move(custom));

        Platform::Print("custom_paint_kits: %s is finish %u (%s)\n", name.c_str(), defIndex, rarityName.c_str());
    }
}

void ItemSchema::ParseItemSets(const KeyValue *itemSetsKey)
{
    m_itemSets.reserve(itemSetsKey->SubkeyCount());

    for (const KeyValue &itemSetKey : *itemSetsKey)
    {
        ItemSet itemSet;
        itemSet.name = std::string{ itemSetKey.GetString("name", itemSetKey.Name()) };
        itemSet.isCollection = itemSetKey.GetNumber("is_collection", false);

        const KeyValue *itemsKey = itemSetKey.GetSubkey("items");
        if (itemsKey)
        {
            itemSet.items.reserve(itemsKey->SubkeyCount());
            for (const KeyValue &itemKey : *itemsKey)
            {
                LootListItem item;
                if (ParseLootListItem(item, itemKey.Name()))
                {
                    itemSet.items.push_back(item);
                }
            }
        }

        m_itemSets.emplace(std::piecewise_construct,
            std::forward_as_tuple(itemSetKey.Name()),
            std::forward_as_tuple(std::move(itemSet)));
    }
}

void ItemSchema::ParsePaintKitRarities(const KeyValue *raritiesKey)
{
    for (const KeyValue &key : *raritiesKey)
    {
        PaintKitInfo *paintKitInfo = PaintKitInfoByName(key.Name());
        if (!paintKitInfo)
        {
            continue;
        }

        assert(paintKitInfo->m_rarity == RarityCommon);
        paintKitInfo->m_rarity = ItemRarityFromString(key.String());
    }
}

void ItemSchema::ParseMusicDefinitions(const KeyValue *musicDefinitionsKey)
{
    m_musicDefinitionInfo.reserve(musicDefinitionsKey->SubkeyCount());

    for (const KeyValue &musicDefinitionKey : *musicDefinitionsKey)
    {
        std::string_view name = musicDefinitionKey.GetString("name");

        m_musicDefinitionInfo.emplace(std::piecewise_construct,
            std::forward_as_tuple(name),
            std::forward_as_tuple(musicDefinitionKey));
    }
}

static void ParseAttributeAndItemName(std::string_view input, std::string_view &attribute, std::string_view &item)
{
    // fallbacks (mikkotodo unfuck)
    attribute = {};
    item = input;

    if (input[0] != '[')
        return;

    size_t attribEnd = input.find(']', 1);
    if (attribEnd == std::string_view::npos)
    {
        assert(false);
        return;
    }

    attribute = input.substr(1, attribEnd - 1);
    item = input.substr(attribEnd + 1);

    assert(attribute.size() && item.size());
}

static LootListItemType LootListItemTypeFromName(std::string_view name, std::string_view attributeName)
{
    if (attributeName.empty())
    {
        return LootListItemNoAttribute;
    }

    const std::pair<std::string_view, LootListItemType> mapNames[] = {
        { "sticker", LootListItemSticker },
        { "spray", LootListItemSpray },
        { "patch", LootListItemPatch },
        { "musickit", LootListItemMusicKit }
    };

    for (const auto &pair : mapNames)
    {
        if (pair.first == name)
        {
            return pair.second;
        }
    }

    return LootListItemPaintable;
}

void ItemSchema::ParseLootLists(const KeyValue *lootListsKey, bool unusual)
{
    m_lootLists.reserve(m_lootLists.size() + lootListsKey->SubkeyCount());

    for (const KeyValue &lootListKey : *lootListsKey)
    {
        auto emplace = m_lootLists.emplace(std::piecewise_construct,
            std::forward_as_tuple(lootListKey.Name()),
            std::forward_as_tuple());

        if (!emplace.second)
        {
            Platform::Print("Duplicate loot list %s ignored\n", std::string{ lootListKey.Name() }.c_str());
            continue;
        }

        LootList &lootList = emplace.first->second;
        lootList.isUnusual = unusual;

        for (const KeyValue &entryKey : lootListKey)
        {
            std::string_view entryName = entryKey.Name();

            // Supplemental GC loot lists can reuse collections from
            // items_game.txt instead of duplicating every painted item.
            if (entryName == "item_sets")
            {
                for (const KeyValue &itemSetKey : entryKey)
                {
                    auto itemSetSearch = m_itemSets.find(std::string{ itemSetKey.Name() });
                    if (itemSetSearch == m_itemSets.end())
                    {
                        Platform::Print("Loot list %s references missing item set %s\n",
                            std::string{ lootListKey.Name() }.c_str(),
                            std::string{ itemSetKey.Name() }.c_str());
                        continue;
                    }

                    for (LootListItem item : itemSetSearch->second.items)
                    {
                        if (unusual)
                        {
                            item.quality = QualityUnusual;
                        }

                        lootList.items.push_back(item);
                    }
                }

                continue;
            }

            // check for options that we ignore
            if (entryName == "will_produce_stattrak")
            {
                lootList.willProduceStatTrak = true;
                continue;
            }

            // check for options that we ignore
            if (entryName == "all_entries_as_additional_drops"
                || entryName == "contains_patches_representing_organizations"
                || entryName == "contains_stickers_autographed_by_proplayers"
                || entryName == "contains_stickers_representing_organizations"
                || entryName == "limit_description_to_number_rnd"
                || entryName == "public_list_contents")
            {
                continue;
            }

            std::string entryNameKey{ entryKey.Name() };

            // check if it's another loot list
            auto listSearch = m_lootLists.find(entryNameKey);
            if (listSearch != m_lootLists.end())
            {
                lootList.subLists.push_back(&listSearch->second);
                continue;
            }

        
            LootListItem item;
            if (ParseLootListItem(item, entryName))
            {
                if (unusual)
                {
                   
                    item.quality = ItemSchema::QualityUnusual;
                }

                lootList.items.push_back(item);
            }
            else
            {
                // what the fuck is this...
                Platform::Print("Unhandled loot list entry %s!!!!\n", entryNameKey.c_str());
            }
        }
    }
}

static uint32_t PaintedItemRarity(uint32_t itemRarity, uint32_t paintKitRarity)
{
    int rarity = (itemRarity - 1) + paintKitRarity;
    if (rarity < 0)
    {
        return 0;
    }

    if (rarity > ItemSchema::RarityAncient)
    {
        if (paintKitRarity == ItemSchema::RarityImmortal)
        {
            return ItemSchema::RarityImmortal;
        }

        return ItemSchema::RarityAncient;
    }

    return rarity;
}

// mikkotodo rewrite this function
bool ItemSchema::ParseLootListItem(LootListItem &item, std::string_view name)
{
    // check for an attribute + item combo
    std::string_view attributeName, itemName;
    ParseAttributeAndItemName(name, attributeName, itemName);

    const ItemInfo *itemInfo = ItemInfoByName(itemName);
    if (!itemInfo)
    {
        Platform::Print("No such item %s!!!\n", std::string{ itemName }.c_str());
        return false;
    }

    item.itemInfo = itemInfo;
    item.type = LootListItemTypeFromName(itemName, attributeName);

    // until proven otherwise...
    item.rarity = itemInfo->m_rarity;
    item.quality = itemInfo->m_quality;

    if (item.type == LootListItemNoAttribute)
    {
        // no attribute
    }
    else if (item.type == LootListItemSticker || item.type == LootListItemSpray || item.type == LootListItemPatch)
    {
        // the attribute is the sticker name
        item.stickerKitInfo = MutableStickerKitInfoByName(attributeName);
        if (!item.stickerKitInfo)
        {
            Platform::Print("WARNING: No such sticker kit %s\n", std::string{ attributeName }.c_str());
            return false;
        }

        // sticker kits affect the item rarity (mikkotodo how do these work, something like PaintedItemRarity???)
        assert(itemInfo->m_rarity == 1);

        if (item.stickerKitInfo->m_rarity)
        {
            item.rarity = item.stickerKitInfo->m_rarity;
        }
    }
    else if (item.type == LootListItemMusicKit)
    {
        // the attribute is the music definition name
        item.musicDefinitionInfo = MusicDefinitionInfoByName(attributeName);
        if (!item.musicDefinitionInfo)
        {
            Platform::Print("WARNING: No such music definition %s\n", std::string{ attributeName }.c_str());
            return false;
        }
    }
    else
    {
        // probably a paint kit
        assert(item.type == LootListItemPaintable);
        item.paintKitInfo = PaintKitInfoByName(attributeName);
        if (!item.paintKitInfo)
        {
            assert(false);
            Platform::Print("WARNING: No such paint kit %s\n", std::string{ attributeName }.c_str());
            return false;
        }

        // paint kits affect the item rarity
        item.rarity = PaintedItemRarity(itemInfo->m_rarity, item.paintKitInfo->m_rarity);
    }

    return true;
}

void ItemSchema::ParseRevolvingLootLists(const KeyValue *revolvingLootListsKey)
{
    m_revolvingLootLists.reserve(revolvingLootListsKey->SubkeyCount());

    for (const KeyValue &revolvingLootListKey : *revolvingLootListsKey)
    {
        uint32_t index = FromString<uint32_t>(revolvingLootListKey.Name());
        assert(index);

        // ugh
        std::string lootListName = std::string{ revolvingLootListKey.String() };

        auto it = m_lootLists.find(lootListName);
        if (it == m_lootLists.end())
        {
            continue;
        }

        m_revolvingLootLists.try_emplace(index, it->second);
    }
}

const ItemInfo *ItemSchema::ItemInfoByName(std::string_view name) const
{
    auto defIndex = m_itemDefIndexByName.find(std::string{ name });
    if (defIndex == m_itemDefIndexByName.end())
    {
        return nullptr;
    }

    return ItemInfoByDefIndex(defIndex->second);
}

const ItemInfo *ItemSchema::ItemInfoByDefIndex(uint32_t defIndex) const
{
    auto it = m_itemInfo.find(defIndex);
    if (it == m_itemInfo.end())
    {
        return nullptr;
    }

    return &it->second;
}

std::optional<TournamentAccessInfo> ItemSchema::TournamentAccessByDefIndex(uint32_t defIndex) const
{
    const ItemInfo *accessItem = ItemInfoByDefIndex(defIndex);
    if (!accessItem || !accessItem->m_tournament.eventId
        || !accessItem->m_name.starts_with("tournament_pass_"))
    {
        return std::nullopt;
    }

    if (std::find(accessItem->m_prefabs.begin(), accessItem->m_prefabs.end(), "fan_token")
        == accessItem->m_prefabs.end())
    {
        return std::nullopt;
    }

    TournamentAccessInfo result;
    result.eventId = accessItem->m_tournament.eventId;

    std::string_view suffix = std::string_view{ accessItem->m_name }.substr(
        std::string_view{ "tournament_pass_" }.size());
    if (suffix.ends_with("_charge"))
    {
        result.type = TournamentAccessType::Token;
        suffix.remove_suffix(std::string_view{ "_charge" }.size());
    }
    else if (suffix.ends_with("_pack"))
    {
        result.type = TournamentAccessType::PassWithTokens;
        result.includedTokens = 3;
        suffix.remove_suffix(std::string_view{ "_pack" }.size());
    }
    else
    {
        result.type = TournamentAccessType::Pass;
    }

    const std::string journalName = std::string{ "tournament_journal_" } + std::string{ suffix };
    const ItemInfo *journal = ItemInfoByName(journalName);
    if (!journal || journal->m_tournament.eventId != result.eventId
        || std::find(journal->m_prefabs.begin(), journal->m_prefabs.end(), "fan_shield")
            == journal->m_prefabs.end())
    {
        return std::nullopt;
    }

    result.journalDefIndex = journal->m_defIndex;
    return result;
}

std::optional<SeasonPassInfo> ItemSchema::SeasonPassByDefIndex(uint32_t defIndex) const
{
    const ItemInfo *pass = ItemInfoByDefIndex(defIndex);
    if (!pass || pass->m_toolType != "season_pass" || !pass->m_seasonAccess)
    {
        return std::nullopt;
    }

    auto coin = m_operationCoinDefIndexBySeason.find(*pass->m_seasonAccess);
    if (coin == m_operationCoinDefIndexBySeason.end())
    {
        return std::nullopt;
    }

    return SeasonPassInfo{ *pass->m_seasonAccess, coin->second };
}

std::optional<uint32_t> ItemSchema::OperationCoinDefIndex(uint32_t season) const
{
    auto coin = m_operationCoinDefIndexBySeason.find(season);
    if (coin == m_operationCoinDefIndexBySeason.end())
    {
        return std::nullopt;
    }
    return coin->second;
}

bool ItemSchema::IsSeasonalMissionCard(uint32_t seasonValue, uint32_t missionCardId) const
{
    auto operation = m_missionCardsBySeason.find(seasonValue);
    if (operation == m_missionCardsBySeason.end())
    {
        return false;
    }

    return std::find(operation->second.begin(), operation->second.end(), missionCardId)
        != operation->second.end();
}

StickerKitInfo *ItemSchema::MutableStickerKitInfoByName(std::string_view name)
{
    auto it = m_stickerKitInfo.find(std::string{ name });
    if (it == m_stickerKitInfo.end())
    {
        assert(false);
        return nullptr;
    }

    return &it->second;
}

const StickerKitInfo *ItemSchema::FindStickerKitInfoByName(std::string_view name) const
{
    auto it = m_stickerKitInfo.find(std::string{ name });
    if (it == m_stickerKitInfo.end())
    {
        return nullptr;
    }

    return &it->second;
}

std::vector<const StickerKitInfo *> ItemSchema::TournamentStickerKits(
    TournamentStickerRole role, uint32_t eventId, uint32_t subjectId) const
{
    std::vector<const StickerKitInfo *> result;
    for (const auto &pair : m_stickerKitInfo)
    {
        const StickerKitInfo &info = pair.second;
        if (info.m_tournamentEventId != eventId)
        {
            continue;
        }

        bool matchesRole = false;
        switch (role)
        {
        case TournamentStickerRole::Event:
            matchesRole = info.m_tournamentTeamId == 0 && info.m_tournamentPlayerId == 0;
            break;

        case TournamentStickerRole::Team:
            matchesRole = info.m_tournamentTeamId == subjectId && info.m_tournamentPlayerId == 0;
            break;

        case TournamentStickerRole::Player:
            matchesRole = info.m_tournamentPlayerId == subjectId;
            break;
        }

        if (matchesRole)
        {
            result.push_back(&info);
        }
    }

    return result;
}

const StickerKitInfo *ItemSchema::StickerKitInfoByDefIndex(uint32_t defIndex) const
{
    for (const auto &pair : m_stickerKitInfo)
    {
        if (pair.second.m_defIndex == defIndex)
        {
            return &pair.second;
        }
    }

    return nullptr;
}

PaintKitInfo *ItemSchema::PaintKitInfoByName(std::string_view name)
{
    auto it = m_paintKitInfo.find(std::string{ name });
    if (it == m_paintKitInfo.end())
    {
        return nullptr;
    }

    return &it->second;
}

const PaintKitInfo *ItemSchema::FindPaintKitByName(std::string_view name) const
{
    auto it = m_paintKitInfo.find(std::string{ name });
    return it == m_paintKitInfo.end() ? nullptr : &it->second;
}

const PaintKitInfo *ItemSchema::PaintKitInfoByDefIndex(uint32_t defIndex) const
{
    for (const auto &pair : m_paintKitInfo)
    {
        if (pair.second.m_defIndex == defIndex)
        {
            return &pair.second;
        }
    }

    return nullptr;
}

MusicDefinitionInfo *ItemSchema::MusicDefinitionInfoByName(std::string_view name)
{
    auto it = m_musicDefinitionInfo.find(std::string{ name });
    if (it == m_musicDefinitionInfo.end())
    {
        assert(false);
        return nullptr;
    }

    return &it->second;
}

const MusicDefinitionInfo *ItemSchema::MusicDefinitionInfoByDefIndex(uint32_t defIndex) const
{
    for (const auto &pair : m_musicDefinitionInfo)
    {
        if (pair.second.m_defIndex == defIndex)
        {
            return &pair.second;
        }
    }

    return nullptr;
}

bool ItemSchema::GetCollectionsForPaintedItem(uint32_t defIndex, uint32_t paintKitDefIndex,
    std::vector<std::string> &outCollections) const
{
    outCollections.clear();

    for (const auto &pair : m_itemSets)
    {
        const ItemSet &itemSet = pair.second;
        if (!itemSet.isCollection || itemSet.isCustomCase)
        {
            continue;
        }

        for (const LootListItem &item : itemSet.items)
        {
            if (!item.itemInfo || !item.paintKitInfo)
            {
                continue;
            }

            if (item.itemInfo->m_defIndex == defIndex && item.paintKitInfo->m_defIndex == paintKitDefIndex)
            {
                outCollections.push_back(pair.first);
                break;
            }
        }
    }

    return !outCollections.empty();
}

bool ItemSchema::GetCollectionsForPaintKit(uint32_t paintKitDefIndex,
    std::vector<std::string> &outCollections) const
{
    outCollections.clear();

    for (const auto &pair : m_itemSets)
    {
        const ItemSet &itemSet = pair.second;
        if (!itemSet.isCollection || itemSet.isCustomCase)
        {
            continue;
        }

        for (const LootListItem &item : itemSet.items)
        {
            if (!item.paintKitInfo)
            {
                continue;
            }

            if (item.paintKitInfo->m_defIndex == paintKitDefIndex)
            {
                outCollections.push_back(pair.first);
                break;
            }
        }
    }

    return !outCollections.empty();
}

bool ItemSchema::GetCustomCaseCollections(uint32_t defIndex, uint32_t paintKitDefIndex,
    std::vector<std::string> &outCollections) const
{
    outCollections.clear();

    for (const auto &pair : m_itemSets)
    {
        if (!pair.second.isCustomCase)
        {
            continue;
        }

        for (const LootListItem &item : pair.second.items)
        {
            if (item.itemInfo && item.paintKitInfo && item.itemInfo->m_defIndex == defIndex
                && item.paintKitInfo->m_defIndex == paintKitDefIndex)
            {
                outCollections.push_back(pair.first);
                break;
            }
        }
    }

    std::sort(outCollections.begin(), outCollections.end());
    return !outCollections.empty();
}

bool ItemSchema::GetCustomCaseRarity(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t &outRarity) const
{
    std::vector<std::string> collections;
    if (!GetCustomCaseCollections(defIndex, paintKitDefIndex, collections))
    {
        return false;
    }

    // the first one, the same collection a trade-up picks
    for (const LootListItem &item : m_itemSets.at(collections.front()).items)
    {
        if (item.itemInfo && item.paintKitInfo && item.itemInfo->m_defIndex == defIndex
            && item.paintKitInfo->m_defIndex == paintKitDefIndex)
        {
            outRarity = item.rarity;
            return true;
        }
    }

    return false;
}

std::string ItemSchema::GetCollectionDisplayName(std::string_view collectionName) const
{
    auto it = m_itemSets.find(std::string{ collectionName });
    if (it == m_itemSets.end())
    {
        return std::string{ collectionName };
    }

    if (it->second.name.empty())
    {
        return std::string{ collectionName };
    }

    return it->second.name;
}

bool ItemSchema::GetTradeUpCandidates(std::string_view collectionName, uint32_t outputRarity,
    std::vector<const LootListItem *> &outCandidates) const
{
    outCandidates.clear();

    auto it = m_itemSets.find(std::string{ collectionName });
    if (it == m_itemSets.end())
    {
        return false;
    }

    const ItemSet &itemSet = it->second;
    for (const LootListItem &item : itemSet.items)
    {
        if (item.type != LootListItemPaintable)
        {
            continue;
        }

        if (!item.itemInfo || !item.paintKitInfo)
        {
            continue;
        }

        if (item.rarity == outputRarity)
        {
            outCandidates.push_back(&item);
        }
    }

    return !outCandidates.empty();
}

uint32_t ItemSchema::GetPaintedRarity(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t fallbackRarity) const
{
    const ItemInfo *itemInfo = ItemInfoByDefIndex(defIndex);
    const PaintKitInfo *paintKitInfo = PaintKitInfoByDefIndex(paintKitDefIndex);

    if (!itemInfo || !paintKitInfo)
    {
        return fallbackRarity;
    }

    return PaintedItemRarity(itemInfo->m_rarity, paintKitInfo->m_rarity);
}

static char LowerAscii(char c)
{
    if (c >= 'A' && c <= 'Z')
    {
        return c - 'A' + 'a';
    }

    return c;
}

static bool ContainsInsensitive(std::string_view haystack, std::string_view needle)
{
    if (needle.empty() || haystack.size() < needle.size())
    {
        return false;
    }

    for (size_t i = 0; i <= haystack.size() - needle.size(); i++)
    {
        bool match = true;
        for (size_t j = 0; j < needle.size(); j++)
        {
            if (LowerAscii(haystack[i + j]) != LowerAscii(needle[j]))
            {
                match = false;
                break;
            }
        }

        if (match)
        {
            return true;
        }
    }

    return false;
}

static bool EqualsInsensitive(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (size_t i = 0; i < left.size(); i++)
    {
        if (LowerAscii(left[i]) != LowerAscii(right[i]))
        {
            return false;
        }
    }

    return true;
}

static bool ItemInfoContains(const ItemInfo &info, std::string_view text)
{
    if (ContainsInsensitive(info.m_name, text) || ContainsInsensitive(info.m_itemType, text))
    {
        return true;
    }

    for (const std::string &prefab : info.m_prefabs)
    {
        if (ContainsInsensitive(prefab, text))
        {
            return true;
        }
    }

    return false;
}

static bool ItemInfoMatchesIdentifier(const ItemInfo &info, std::string_view text)
{
    if (EqualsInsensitive(info.m_name, text) || EqualsInsensitive(info.m_itemType, text))
    {
        return true;
    }

    for (const std::string &prefab : info.m_prefabs)
    {
        if (EqualsInsensitive(prefab, text))
        {
            return true;
        }
    }

    return false;
}

bool ItemSchema::IsKeyToolDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    if (!info)
    {
        return false;
    }

    return ItemInfoMatchesIdentifier(*info, "weapon_case_key")
        || ItemInfoMatchesIdentifier(*info, "weaponcasekey")
        || ItemInfoMatchesIdentifier(*info, "#CSGO_Type_WeaponCaseKey")
        || ItemInfoMatchesIdentifier(*info, "CSGO_Type_WeaponCaseKey");
}

bool ItemSchema::IsNameTagToolDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    if (!info)
    {
        return false;
    }

    return ItemInfoContains(*info, "name tag")
        || ItemInfoContains(*info, "name_tag")
        || ItemInfoContains(*info, "nametag");
}

bool ItemSchema::IsStatTrakSwapToolDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    if (!info)
    {
        return false;
    }

    return ItemInfoContains(*info, "stattrak")
        && ItemInfoContains(*info, "swap");
}

bool ItemSchema::IsKeyCompatibleWithCrate(uint32_t keyDefIndex, uint32_t crateDefIndex) const
{
    const ItemInfo *keyInfo = ItemInfoByDefIndex(keyDefIndex);
    const ItemInfo *crateInfo = ItemInfoByDefIndex(crateDefIndex);
    if (!keyInfo || !crateInfo)
    {
        return false;
    }

    if (keyInfo->m_toolRestriction.empty() || crateInfo->m_toolRestriction.empty())
    {
        return true;
    }

    return keyInfo->m_toolRestriction == crateInfo->m_toolRestriction;
}

bool ItemSchema::CanApplyStickerToDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    return info && info->m_canSticker;
}

bool ItemSchema::CanApplyPatchToDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    return info && info->m_canPatch;
}

bool ItemSchema::CanNameDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    return info && info->m_nameable;
}

bool ItemSchema::CanStatTrakSwapDefIndex(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    return info && info->m_canStatTrakSwap;
}

std::vector<uint32_t> ItemSchema::PrestigeMedalDefIndexes(uint32_t year) const
{
    std::vector<uint32_t> defIndexes;
    for (const auto &[defIndex, info] : m_itemInfo)
    {
        if (info.m_itemType == "prestige_coin" && info.m_prestigeYear == year)
        {
            defIndexes.push_back(defIndex);
        }
    }

    std::sort(defIndexes.begin(), defIndexes.end());
    return defIndexes;
}
namespace
{
    // (def index, paint kit) identifies a skin; paint kit is 0 for vanilla knives
    uint64_t PaintedItemKey(const LootListItem &item)
    {
        const uint32_t def = item.itemInfo ? item.itemInfo->m_defIndex : 0;
        const uint32_t kit = item.paintKitInfo ? item.paintKitInfo->m_defIndex : 0;
        return (static_cast<uint64_t>(def) << 32) | kit;
    }

    // Walk a crate's loot list tree, splitting entries into regular skins and
    // "unusual" ones (knives/gloves).
    void FlattenLootList(const LootList &list,
        std::vector<const LootListItem *> &regular,
        std::vector<const LootListItem *> &special,
        std::unordered_set<const LootList *> &visited)
    {
        if (!visited.insert(&list).second)
        {
            return;
        }

        for (const LootListItem &item : list.items)
        {
            if (list.isUnusual || item.CaseRarity() == ItemSchema::RarityUnusual)
            {
                special.push_back(&item);
            }
            else
            {
                regular.push_back(&item);
            }
        }

        for (const LootList *sub : list.subLists)
        {
            if (sub)
            {
                FlattenLootList(*sub, regular, special, visited);
            }
        }
    }
}

// Collections and cases are linked through the skins: a case belongs to a
// collection if the case's regular skins include the collection's skins. The
// collection's gold pool is that case's unusual list. Built lazily on first use
// so it always runs after the schema is fully loaded.
void ItemSchema::BuildSpecialTradeUpPools() const
{
    if (m_specialPoolsBuilt)
    {
        return;
    }
    m_specialPoolsBuilt = true;

    struct CratePool
    {
        std::unordered_set<uint64_t> regularKeys;
        std::vector<const LootListItem *> special;
    };

    std::vector<CratePool> crates;
    for (const auto &pair : m_itemInfo)
    {
        const ItemInfo &info = pair.second;
        // weapon cases point at their contents through a supply crate series,
        // other containers through a loot list name
        if (info.m_lootListName.empty() && !info.m_supplyCrateSeries)
        {
            continue;
        }

        // link collections through the real contents, not custom_cases.txt
        const LootList *root = GetOriginalCrateLootList(info.m_defIndex);
        if (!root)
        {
            continue;
        }

        std::vector<const LootListItem *> regular;
        CratePool crate;
        std::unordered_set<const LootList *> visited;
        FlattenLootList(*root, regular, crate.special, visited);

        // souvenir packages, sticker capsules etc. have no special items
        if (crate.special.empty())
        {
            continue;
        }

        for (const LootListItem *item : regular)
        {
            crate.regularKeys.insert(PaintedItemKey(*item));
        }
        crates.push_back(std::move(crate));
        m_dropCrateDefIndexes.push_back(info.m_defIndex);
    }

    for (const auto &pair : m_itemSets)
    {
        const ItemSet &set = pair.second;
        if (set.isCustomCase)
        {
            continue;
        }

        std::vector<const LootListItem *> pool;
        std::unordered_set<uint64_t> seen;

        for (const CratePool &crate : crates)
        {
            bool linked = false;
            for (const LootListItem &setItem : set.items)
            {
                if (crate.regularKeys.count(PaintedItemKey(setItem)))
                {
                    linked = true;
                    break;
                }
            }

            if (!linked)
            {
                continue;
            }

            // several crate defs can share a list, so dedupe
            for (const LootListItem *special : crate.special)
            {
                if (seen.insert(PaintedItemKey(*special)).second)
                {
                    pool.push_back(special);
                }
            }
        }

        if (!pool.empty())
        {
            m_specialPoolByCollection.emplace(pair.first, std::move(pool));
        }
    }

    // a custom case's knives and gloves are its own pool
    for (const auto &pair : m_customCaseGoldLists)
    {
        std::vector<const LootListItem *> pool;
        for (const LootListItem &item : pair.second->items)
        {
            if (item.itemInfo)
            {
                pool.push_back(&item);
            }
        }

        if (!pool.empty())
        {
            m_specialPoolByCollection[pair.first] = std::move(pool);
        }
    }
}

bool ItemSchema::GetSpecialTradeUpCandidates(std::string_view collectionName,
    std::vector<const LootListItem *> &outCandidates) const
{
    BuildSpecialTradeUpPools();

    auto it = m_specialPoolByCollection.find(std::string(collectionName));
    if (it == m_specialPoolByCollection.end())
    {
        return false;
    }

    outCandidates = it->second;
    return !outCandidates.empty();
}

const std::vector<uint32_t> &ItemSchema::GetDropCrateDefIndexes() const
{
    BuildSpecialTradeUpPools();
    return m_dropCrateDefIndexes;
}

std::vector<uint32_t> ItemSchema::GetAgentDefIndexes() const
{
    // agents use the customplayertradable prefab (the default T/CT models use
    // plain customplayer, and aren't things you can own)
    std::vector<uint32_t> agents;
    for (const auto &pair : m_itemInfo)
    {
        const ItemInfo &info = pair.second;
        if (info.m_isCoupon || info.m_name.empty())
        {
            continue;
        }

        const bool tradable = std::any_of(info.m_prefabs.begin(), info.m_prefabs.end(),
            [](const std::string &prefab) { return EqualsInsensitive(prefab, "customplayertradable"); });
        if (tradable)
        {
            agents.push_back(info.m_defIndex);
        }
    }

    std::sort(agents.begin(), agents.end());
    return agents;
}

std::vector<uint32_t> ItemSchema::GetContainerDefIndexes() const
{
    std::vector<uint32_t> containers;
    for (const auto &pair : m_itemInfo)
    {
        const ItemInfo &info = pair.second;
        if (info.m_isCoupon || IsKeyToolDefIndex(info.m_defIndex))
        {
            continue;
        }

        // containers are named crate_* in items_game.txt and have contents
        const bool namedCrate = info.m_name.compare(0, 6, "crate_") == 0;
        if (!namedCrate || (!info.m_supplyCrateSeries && info.m_lootListName.empty()))
        {
            continue;
        }

        if (CanCreateItem(info.m_defIndex))
        {
            containers.push_back(info.m_defIndex);
        }
    }

    std::sort(containers.begin(), containers.end());
    return containers;
}

ItemSchema::ContainerKind ItemSchema::GetContainerKind(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    if (!info)
    {
        return ContainerKind::Other;
    }

    // weapon cases are the containers with a knife/glove pool
    const std::vector<uint32_t> &cases = GetDropCrateDefIndexes();
    if (std::find(cases.begin(), cases.end(), defIndex) != cases.end())
    {
        return ContainerKind::WeaponCase;
    }

    auto anyPrefabContains = [&](std::string_view text)
    {
        for (const std::string &prefab : info->m_prefabs)
        {
            if (prefab.find(text) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    };

    if (anyPrefabContains("souvenir"))
    {
        return ContainerKind::SouvenirPackage;
    }

    const std::string &name = info->m_name;
    if (anyPrefabContains("sticker_capsule") || anyPrefabContains("signature_capsule")
        || anyPrefabContains("patch_capsule")
        || name.compare(0, 18, "crate_sticker_pack") == 0
        || name.compare(0, 20, "crate_signature_pack") == 0
        || name.compare(0, 16, "crate_patch_pack") == 0)
    {
        return ContainerKind::Capsule;
    }

    return ContainerKind::Other;
}

bool ItemSchema::IsSingleItemCoupon(uint32_t defIndex) const
{
    const ItemInfo *info = ItemInfoByDefIndex(defIndex);
    if (!info || !info->m_isCoupon)
    {
        return false;
    }

    auto it = m_lootLists.find(info->m_lootListName);
    if (it == m_lootLists.end() || it->second.items.size() != 1 || !it->second.subLists.empty())
    {
        return false;
    }

    // a coupon for a container (a capsule, a collection package...) is a pack, keep it
    const LootListItem &item = it->second.items.front();
    if (!item.itemInfo)
    {
        return false;
    }
    return !(item.itemInfo->m_supplyCrateSeries || !item.itemInfo->m_lootListName.empty());
}

std::vector<uint32_t> ItemSchema::GetKeysForCrate(uint32_t crateDefIndex) const
{
    std::vector<uint32_t> keys;
    const ItemInfo *crateInfo = ItemInfoByDefIndex(crateDefIndex);
    if (!crateInfo || crateInfo->m_toolRestriction.empty())
    {
        return keys;
    }

    for (const auto &pair : m_itemInfo)
    {
        const ItemInfo &info = pair.second;
        if (info.m_toolRestriction == crateInfo->m_toolRestriction && IsKeyToolDefIndex(info.m_defIndex))
        {
            keys.push_back(info.m_defIndex);
        }
    }

    std::sort(keys.begin(), keys.end());
    return keys;
}

bool ItemSchema::GetDropSkinCandidates(uint32_t rarity, std::vector<const LootListItem *> &outCandidates) const
{
    BuildSpecialTradeUpPools();

    std::vector<const LootListItem *> caseCollectionCandidates;
    outCandidates.clear();

    for (const auto &pair : m_itemSets)
    {
        const ItemSet &itemSet = pair.second;
        if (!itemSet.isCollection || itemSet.isCustomCase)
        {
            continue;
        }

        const bool fromCase = m_specialPoolByCollection.count(pair.first) != 0;
        for (const LootListItem &item : itemSet.items)
        {
            if (item.type != LootListItemPaintable || !item.itemInfo || !item.paintKitInfo
                || item.rarity != rarity)
            {
                continue;
            }

            (fromCase ? caseCollectionCandidates : outCandidates).push_back(&item);
        }
    }

    if (outCandidates.empty())
    {
        outCandidates = std::move(caseCollectionCandidates);
    }

    return !outCandidates.empty();
}

bool ItemSchema::GetSpecialTradeUpCollections(uint32_t defIndex, uint32_t paintKitDefIndex,
    std::vector<std::string> &outCollections) const
{
    BuildSpecialTradeUpPools();

    outCollections.clear();
    const uint64_t key = (static_cast<uint64_t>(defIndex) << 32) | paintKitDefIndex;
    for (const auto &pair : m_specialPoolByCollection)
    {
        for (const LootListItem *item : pair.second)
        {
            if (PaintedItemKey(*item) == key)
            {
                outCollections.push_back(pair.first);
                break;
            }
        }
    }

    return !outCollections.empty();
}
