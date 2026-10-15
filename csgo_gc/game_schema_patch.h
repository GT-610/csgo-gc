#pragma once

#include <map>
#include <string>
#include <vector>

// Optional patching of the game's own items_game.txt so that the case contents
// preview (which the client builds from that file) matches custom_cases.txt.
// The original file is backed up once and every patch starts from the backup;
// the GC always reads the original so collection links stay intact.
namespace GameSchemaPatch
{
    constexpr const char *SchemaPath = "csgo/scripts/items/items_game.txt";
    constexpr const char *BackupPath = "csgo/scripts/items/items_game.txt.csgo_gc_backup";

    // the items_game.txt the GC should parse: the original when a backup exists
    const char *OriginalSchemaPath();

    // loot list name -> entries ("[paint_kit]weapon") that replace its contents
    using LootListPreviews = std::map<std::string, std::vector<std::string>>;

    // replaces the bodies of the named lists inside items_game/client_loot_lists.
    // returns the number of lists replaced
    size_t PatchLootLists(std::string &schemaText, const LootListPreviews &previews);

    // trade-up recipes the final CS:GO schema lacks: 5 Coverts -> knife/glove
    // (5, StatTrak 15) and 5 knives/gloves -> knife/glove (6)
    extern const char *const GoldTradeUpRecipes;

    // inserts recipe definitions at the end of items_game/recipes. returns false
    // when there is no recipes block
    bool InsertRecipes(std::string &schemaText, const std::string &recipesText);

    // lets trade-up recipes that take normal items (quality "unique") take
    // StatTrak and Souvenir items too, so they can be mixed in one contract.
    // knives/gloves (quality "unusual") stay out. returns the number of recipe
    // conditions changed
    size_t RelaxTradeUpQuality(std::string &schemaText);

    // replaces the goods on one operation's page: removes every
    // operational_point_redeemable block of items_game/seasonaloperations/<season>
    // and adds the given blocks instead. returns the number of blocks removed,
    // or -1 when that season isn't in the file
    int ReplaceRedeemables(std::string &schemaText, uint32_t season, const std::string &blocksText);

    // The game only shows an operation's page while the operation is running,
    // which it works out from items_game/quest_schedule (start, length, slack).
    // Riptide's schedule is long over, so move its start to just before now when
    // now isn't inside the window. returns 1 when the start was moved, 0 when it
    // was already running, -1 when there is no quest_schedule start
    int ActivateOperation(std::string &schemaText, int64_t now);

    // the star shop's operation page goods (see StarShop::BuildRedeemables)
    struct StarShopPatch
    {
        uint32_t season{};
        std::string blocksText;
        bool activateOperation{}; // see ActivateOperation
    };

    // a case custom_cases.txt adds: a copy of an existing case's item with its own
    // def index, name, series and contents list
    struct AddedCasePatch
    {
        uint32_t defIndex{};
        uint32_t basedOn{};
        uint32_t series{};
        std::string name;
        std::vector<std::string> previewEntries; // "[paint_kit]weapon"
    };

    // adds the cases to items_game/items (copied from the case they're based on),
    // items_game/revolving_loot_lists and items_game/client_loot_lists. returns
    // how many were added; one whose base case isn't in the file is skipped
    size_t AddCases(std::string &schemaText, const std::vector<AddedCasePatch> &cases);

    // writes the patched schema (backing up the original first). returns true
    // when the game file changed and a restart is needed to see it
    bool Apply(const LootListPreviews &previews, const std::string &recipesText, bool mixQualities = false,
        const StarShopPatch *starShop = nullptr, const std::vector<AddedCasePatch> *addedCases = nullptr);

    // puts the original file back if it was patched. returns true when restored
    bool Restore();
}
