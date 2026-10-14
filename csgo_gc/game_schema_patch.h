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

    // writes the patched schema (backing up the original first). returns true
    // when the game file changed and a restart is needed to see it
    bool Apply(const LootListPreviews &previews, const std::string &recipesText, bool mixQualities = false);

    // puts the original file back if it was patched. returns true when restored
    bool Restore();
}
