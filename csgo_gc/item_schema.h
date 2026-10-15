#pragma once

#include "gc_const_csgo.h"

class KeyValue;
class Random;

enum class AttributeType
{
    Float,
    Uint32,
    String
};

class AttributeInfo
{
public:
    explicit AttributeInfo(const KeyValue &key);

    AttributeType m_type;
};

struct TournamentMetadata
{
    uint32_t eventId{};
    uint32_t stageId{};
    uint32_t team0Id{};
    uint32_t team1Id{};
    uint32_t mvpAccountId{};
};

struct GeneratedItemAttribute
{
    uint32_t defIndex{};
    std::string value;
};

enum class TournamentAccessType
{
    Pass,
    PassWithTokens,
    Token
};

struct TournamentAccessInfo
{
    TournamentAccessType type{};
    uint32_t eventId{};
    uint32_t journalDefIndex{};
    uint32_t includedTokens{};
};

struct SeasonPassInfo
{
    uint32_t seasonValue{};
    uint32_t coinDefIndex{};
};

class ItemInfo
{
public:
    explicit ItemInfo(uint32_t defIndex);

    uint32_t m_defIndex;
    std::string m_name;
    uint32_t m_rarity;
    uint32_t m_quality;
    uint32_t m_level;
    uint32_t m_supplyCrateSeries; // cases only
    uint32_t m_stickerSlotCount;
    uint32_t m_prestigeYear;
    TournamentMetadata m_tournament;
    std::string m_itemType;
    std::vector<std::string> m_prefabs;
    std::vector<GeneratedItemAttribute> m_generatedAttributes;
    std::string m_toolType;
    std::string m_toolRestriction;
    std::optional<uint32_t> m_seasonAccess;
    bool m_canSticker;
    bool m_canPatch;
    bool m_nameable;
    bool m_canStatTrakSwap;

    // kludge for coupons so we can buy stuff from the store
    bool m_isCoupon;
    std::string m_lootListName;
    bool m_willProduceStatTrak;
};

class PaintKitInfo
{
public:
    PaintKitInfo(const KeyValue &key, float defaultMinFloat, float defaultMaxFloat);

    uint32_t m_defIndex;
    uint32_t m_rarity;
    float m_minFloat;
    float m_maxFloat;
};

class StickerKitInfo
{
public:
    explicit StickerKitInfo(const KeyValue &key);

    uint32_t m_defIndex;
    std::string m_name;
    uint32_t m_rarity;
    uint32_t m_tournamentEventId;
    uint32_t m_tournamentTeamId;
    uint32_t m_tournamentPlayerId;
};

enum class TournamentStickerRole
{
    Event,
    Team,
    Player,
};

class MusicDefinitionInfo
{
public:
    MusicDefinitionInfo(const KeyValue &key);

    uint32_t m_defIndex;
};

enum LootListItemType
{
    LootListItemNoAttribute,
    LootListItemPaintable,
    LootListItemSticker,
    LootListItemSpray,
    LootListItemPatch,
    LootListItemMusicKit,
};

struct LootListItem
{
    // for case opening: returns RarityUnusual for items of unusual quality
    uint32_t CaseRarity() const;

    const ItemInfo *itemInfo{};
    LootListItemType type{ LootListItemNoAttribute };

    // these could be sticked into a variant to save a grand total of few bytes
    const PaintKitInfo *paintKitInfo{};
    const StickerKitInfo *stickerKitInfo{};
    const MusicDefinitionInfo *musicDefinitionInfo{};

    // might differ from those specified in itemInfo
    // (based on paint kits, stattrak etc.)
    uint32_t rarity{};
    uint32_t quality{};
};

struct LootList
{
    // we either have items or sublists, never both
    std::vector<LootListItem> items;
    std::vector<const LootList *> subLists;
    bool willProduceStatTrak{};
    bool isUnusual{};
};

struct ItemSet
{
    std::string name;
    bool isCollection{};
    // made from a custom case; official collections win when both know a skin
    bool isCustomCase{};
    std::vector<LootListItem> items;
};

class ItemSchema
{
public:
    ItemSchema();

    float AttributeFloat(const CSOEconItemAttribute *attribute) const;
    uint32_t AttributeUint32(const CSOEconItemAttribute *attribute) const;
    std::string AttributeString(const CSOEconItemAttribute *attribute) const;

    bool SetAttributeFloat(CSOEconItemAttribute *attribute, float value) const;
    bool SetAttributeUint32(CSOEconItemAttribute *attribute, uint32_t value) const;
    bool SetAttributeString(CSOEconItemAttribute *attribute, std::string_view value) const;

    // for case opening
    const LootList *GetCrateLootList(uint32_t crateDefIndex) const;
    // the crate's contents from the game schema, ignoring custom_cases.txt
    const LootList *GetOriginalCrateLootList(uint32_t crateDefIndex) const;

    // Legacy souvenir packages are opened through UnlockCrate with no key.
    // Distinguish them from other self-opening containers using schema data.
    bool IsSouvenirPackage(const CSOEconItem &item) const;

    // for case opening
    bool CreateItemFromLootListItem(Random &random,
        const LootListItem &lootListItem,
        bool statTrak,
        ItemOrigin origin,
        UnacknowledgedType unacknowledgedType,
        CSOEconItem &item) const;

    // item creation: id and account id not set, needs to be done by the caller
    bool CanCreateItem(uint32_t defIndex) const;
    bool CreateItem(uint32_t defIndex, ItemOrigin origin, UnacknowledgedType unacknowledgedType, CSOEconItem &econItem) const;

    // trade-up helpers
    const ItemInfo *ItemInfoByDefIndex(uint32_t defIndex) const;
    const ItemInfo *ItemInfoByName(std::string_view name) const;
    std::optional<TournamentAccessInfo> TournamentAccessByDefIndex(uint32_t defIndex) const;
    std::optional<SeasonPassInfo> SeasonPassByDefIndex(uint32_t defIndex) const;
    // the newest operation in the schema (what the star shop uses) and its coin
    std::optional<uint32_t> LatestSeasonalOperation() const { return m_latestSeason; }
    std::optional<uint32_t> OperationCoinDefIndex(uint32_t season) const;
    bool IsSeasonalMissionCard(uint32_t seasonValue, uint32_t missionCardId) const;
    const PaintKitInfo *PaintKitInfoByDefIndex(uint32_t defIndex) const;
    const PaintKitInfo *FindPaintKitByName(std::string_view name) const;
    const StickerKitInfo *StickerKitInfoByDefIndex(uint32_t defIndex) const;
    const MusicDefinitionInfo *MusicDefinitionInfoByDefIndex(uint32_t defIndex) const;
    const StickerKitInfo *FindStickerKitInfoByName(std::string_view name) const;
    std::vector<const StickerKitInfo *> TournamentStickerKits(
        TournamentStickerRole role, uint32_t eventId, uint32_t subjectId = 0) const;
    bool GetCollectionsForPaintedItem(uint32_t defIndex, uint32_t paintKitDefIndex,
        std::vector<std::string> &outCollections) const;
    bool GetCollectionsForPaintKit(uint32_t paintKitDefIndex,
        std::vector<std::string> &outCollections) const;
    // custom cases act as collections for trade-ups: their skins trade up inside the
    // case and use the rarity the case gave them
    bool GetCustomCaseCollections(uint32_t defIndex, uint32_t paintKitDefIndex,
        std::vector<std::string> &outCollections) const;
    bool GetCustomCaseRarity(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t &outRarity) const;
    std::string GetCollectionDisplayName(std::string_view collectionName) const;
    bool GetTradeUpCandidates(std::string_view collectionName, uint32_t outputRarity,
        std::vector<const LootListItem *> &outCandidates) const;
    // gold contract: knives/gloves obtainable from the case(s) tied to a collection
    bool GetSpecialTradeUpCandidates(std::string_view collectionName,
        std::vector<const LootListItem *> &outCandidates) const;
    // match drops: weapon cases (crates that can contain a knife/glove)
    const std::vector<uint32_t> &GetDropCrateDefIndexes() const;
    // every container (weapon cases, souvenir packages, sticker/patch/pin capsules...)
    std::vector<uint32_t> GetContainerDefIndexes() const;

    // player agents (item_type "customplayer"), sorted by def index
    std::vector<uint32_t> GetAgentDefIndexes() const;

    enum class ContainerKind
    {
        WeaponCase,
        SouvenirPackage,
        Capsule, // sticker, autograph and patch capsules
        Other, // graffiti boxes, pin capsules, music kit boxes, ...
    };
    ContainerKind GetContainerKind(uint32_t defIndex) const;

    // store coupons that give one sticker/graffiti/music kit/... rather than a container
    bool IsSingleItemCoupon(uint32_t defIndex) const;
    // custom_cases.txt display name for a crate, or null
    const std::string *CustomCaseName(uint32_t crateDefIndex) const;
    // star price from the case's own "price" in custom_cases.txt, 0 for the default
    uint32_t CustomCasePrice(uint32_t crateDefIndex) const;
    // cases custom_cases.txt adds as new items (a name instead of a def index,
    // with "based_on" naming the case to copy the look and key from)
    struct AddedCase
    {
        uint32_t defIndex;
        uint32_t basedOn;
        uint32_t series; // supply crate series, unique to the new case
        std::string name; // items_game name, e.g. crate_csgogc_gold_rush
        std::vector<std::string> previewEntries; // "[paint_kit]weapon" of its regular skins
    };
    const std::vector<AddedCase> &AddedCases() const { return m_addedCases; }

    // finishes from custom_paint_kits.txt: new entries for items_game.txt's
    // paint_kits and paint_kits_rarity, written by the game file patch
    struct CustomPaintKit
    {
        uint32_t defIndex{};
        std::string name;
        std::string title;
        std::string nameToken;  // language token holding the title, empty when the tag is the plain title
        std::string kitText;    // the paint_kits block
        std::string rarityText; // the paint_kits_rarity line
    };
    const std::vector<CustomPaintKit> &CustomPaintKits() const { return m_customPaintKits; }
    bool IsAddedCase(uint32_t crateDefIndex) const;

    // crates whose contents custom_cases.txt replaces (or adds), sorted
    std::vector<uint32_t> GetCustomCaseDefIndexes() const;
    bool IsCustomCase(uint32_t crateDefIndex) const;
    // keys that open this crate (empty when the crate needs no specific key)
    std::vector<uint32_t> GetKeysForCrate(uint32_t crateDefIndex) const;
    // match drops: painted skins of a rarity from collections that aren't tied to a
    // case (map/operation collections, like old CS:GO drops). falls back to all
    // collections when none of those have that rarity
    bool GetDropSkinCandidates(uint32_t rarity, std::vector<const LootListItem *> &outCandidates) const;
    // gold contract: collections whose gold pool contains this knife/glove (paint kit 0 for vanilla)
    bool GetSpecialTradeUpCollections(uint32_t defIndex, uint32_t paintKitDefIndex,
        std::vector<std::string> &outCollections) const;
    uint32_t GetPaintedRarity(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t fallbackRarity) const;
    bool IsKeyToolDefIndex(uint32_t defIndex) const;
    bool IsNameTagToolDefIndex(uint32_t defIndex) const;
    bool IsStatTrakSwapToolDefIndex(uint32_t defIndex) const;
    bool IsKeyCompatibleWithCrate(uint32_t keyDefIndex, uint32_t crateDefIndex) const;
    bool CanApplyStickerToDefIndex(uint32_t defIndex) const;
    bool CanApplyPatchToDefIndex(uint32_t defIndex) const;
    bool CanNameDefIndex(uint32_t defIndex) const;
    bool CanStatTrakSwapDefIndex(uint32_t defIndex) const;
    std::vector<uint32_t> PrestigeMedalDefIndexes(uint32_t year) const;

public:
    // these could be parsed from the item schema but reduce code complexity by hardcoding them
    enum Rarity
    {
        RarityDefault = 0,
        RarityCommon = 1,
        RarityUncommon = 2,
        RarityRare = 3,
        RarityMythical = 4,
        RarityLegendary = 5,
        RarityAncient = 6,
        RarityImmortal = 7,

        RarityUnusual = 99
    };

    enum Quality
    {
        QualityNormal = 0,
        QualityGenuine = 1,
        QualityVintage = 2,
        QualityUnusual = 3,
        QualityUnique = 4,
        QualityCommunity = 5,
        QualityDeveloper = 6,
        QualitySelfmade = 7,
        QualityCustomized = 8,
        QualityStrange = 9,
        QualityCompleted = 10,
        QualityHaunted = 11,
        QualityTournament = 12
    };

    enum GraffitiTint
    {
        GraffitiTintMin = 1,
        GraffitiTintMax = 19
    };

    enum LoadoutSlot
    {
        LoadoutSlotMusicKit = 54,
        LoadoutSlotGraffiti = 56
    };

    enum Item
    {
        ItemCasket = 1201,
        ItemSticker = 1209,
        ItemMusicKit = 1314,
        ItemStatTrakSwapTool = 1324,
        ItemSpray = 1348,
        ItemSprayPaint = 1349,
        ItemStatTrakSwapToolBundle = 4088,
        ItemPatch = 4609,
        ItemStatsSubscription = 4748
    };

    enum Attribute
    {
        AttributeTexturePrefab = 6,
        AttributeTextureSeed = 7,
        AttributeTextureWear = 8,
        AttributeKillEater = 80,
        AttributeKillEaterScoreType = 81,

        AttributeCustomName = 111,

        // ugh
        AttributeStickerId0 = 113,
        AttributeStickerWear0 = 114,
        AttributeStickerScale0 = 115,
        AttributeStickerRotation0 = 116,
        AttributeStickerId1 = 117,
        AttributeStickerWear1 = 118,
        AttributeStickerScale1 = 119,
        AttributeStickerRotation1 = 120,
        AttributeStickerId2 = 121,
        AttributeStickerWear2 = 122,
        AttributeStickerScale2 = 123,
        AttributeStickerRotation2 = 124,
        AttributeStickerId3 = 125,
        AttributeStickerWear3 = 126,
        AttributeStickerScale3 = 127,
        AttributeStickerRotation3 = 128,
        AttributeStickerId4 = 129,
        AttributeStickerWear4 = 130,
        AttributeStickerScale4 = 131,
        AttributeStickerRotation4 = 132,
        AttributeStickerId5 = 133,
        AttributeStickerWear5 = 134,
        AttributeStickerScale5 = 135,
        AttributeStickerRotation5 = 136,

        AttributeMusicId = 166,
        AttributeQuestId = 168,

        AttributeCampaignId = 184,
        AttributeCampaignCompletionBitfield = 185,

        AttributeSpraysRemaining = 232,
        AttributeSprayTintId = 233,
        AttributeOperationDropsAwardedPurchased = 237,
        AttributeOperationDropsAwardedRedeemed = 240,

        AttributeUpgradeLevel = 268,
        AttributeCasketItemsCount = 270,
        AttributeCasketModificationDate = 271,
        AttributeCasketIdLow = 272,
        AttributeCasketIdHigh = 273,

        AttributeTournamentEventId = 137,
        AttributeTournamentEventStageId = 138,
        AttributeTournamentTeam0Id = 139,
        AttributeTournamentTeam1Id = 140,
        AttributeTournamentMvpAccountId = 223,
    };

private:
    explicit ItemSchema(bool loadFiles);

    void ParseItems(const KeyValue *itemsKey, const KeyValue *prefabsKey);
    void ParseItemRecursive(ItemInfo &info, const KeyValue &itemKey, const KeyValue *prefabsKey);
    void ParseSeasonalOperations(const KeyValue *seasonalOperationsKey);
    void ParseAttributes(const KeyValue *attributesKey);
    void ParseStickerKits(const KeyValue *stickerKitsKey);
    void ParsePaintKits(const KeyValue *paintKitsKey);
    void ParsePaintKitRarities(const KeyValue *raritiesKey);
    void ParseCustomPaintKits(const KeyValue &customKitsKey);
    void ParseMusicDefinitions(const KeyValue *musicDefinitionsKey);
    void ParseItemSets(const KeyValue *itemSetsKey);
    void ParseLootLists(const KeyValue *lootListsKey, bool unusual);
    void ParseRevolvingLootLists(const KeyValue *revolvingLootListsKey);
    void BuildSpecialTradeUpPools() const;
    void ParseCustomCases(const KeyValue &customCasesKey);

    // lazily built gold trade-up pools, keyed by collection
    mutable bool m_specialPoolsBuilt = false;
    mutable std::unordered_map<std::string, std::vector<const LootListItem *>> m_specialPoolByCollection;
    mutable std::vector<uint32_t> m_dropCrateDefIndexes;

    bool ParseLootListItem(LootListItem &item, std::string_view name);
    bool ApplyGeneratedAttributes(const ItemInfo &info, CSOEconItem &item) const;

    // internal slop
    StickerKitInfo *MutableStickerKitInfoByName(std::string_view name);
    PaintKitInfo *PaintKitInfoByName(std::string_view name);
    MusicDefinitionInfo *MusicDefinitionInfoByName(std::string_view name);

    std::unordered_map<uint32_t, ItemInfo> m_itemInfo;
    std::unordered_map<uint32_t, AttributeInfo> m_attributeInfo;
    std::unordered_map<std::string, uint32_t> m_itemDefIndexByName;
    std::unordered_map<std::string, uint32_t> m_attributeDefIndexByName;
    std::unordered_map<uint32_t, uint32_t> m_operationCoinDefIndexBySeason;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_missionCardsBySeason;
    std::optional<uint32_t> m_latestSeason;

    std::unordered_map<std::string, StickerKitInfo> m_stickerKitInfo;
    std::unordered_map<std::string, PaintKitInfo> m_paintKitInfo;
    std::unordered_map<std::string, MusicDefinitionInfo> m_musicDefinitionInfo;
    std::unordered_map<std::string, LootList> m_lootLists;

    std::unordered_map<std::string, ItemSet> m_itemSets;
    // custom case collection name -> its knife/glove list
    std::unordered_map<std::string, const LootList *> m_customCaseGoldLists;

    std::unordered_map<uint32_t, const LootList &> m_revolvingLootLists;
    // custom_cases.txt: crate def index -> replacement contents
    std::unordered_map<uint32_t, const LootList *> m_customCaseLootLists;
    std::unordered_map<uint32_t, std::string> m_customCaseNames;
    std::unordered_map<uint32_t, uint32_t> m_customCasePrices;
    std::map<uint32_t, std::vector<std::string>> m_customCasePreviewEntries;
    std::vector<AddedCase> m_addedCases;
    std::vector<CustomPaintKit> m_customPaintKits;

    friend class ItemSchemaTestFixture;
    friend class SouvenirTestFixture;
};
