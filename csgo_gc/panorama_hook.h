#pragma once

#include <cstdint>
#include <string>

class ItemSchema;

// Small patches to the game's Panorama UI, done in memory while the game runs.
//
// The UI scripts live in the signed csgo/panorama/code.pbin, so they can't be
// edited on disk. Instead this hooks the UI engine's RunScript (every script
// in a layout file goes through it) and:
//   - lets knives and gloves be added to a trade-up contract (the stock
//     "Add to contract" menu entry skips melee/glove slots)
//   - adds a Case Store button to the main menu: one big searchable screen
//     with every container the store sells, bought together with its key
//
// Windows only. Everything is skipped (with a console message) if panorama.dll
// doesn't look like the final CS:GO build.
namespace PanoramaHook
{
    // installs once panorama.dll is loaded, safe to call every frame
    void TryInstall();

    // the containers to list, as built by BuildCaseStoreData. sent to the UI
    // as soon as both the data and the main menu exist
    void SetCaseStoreData(std::string data);
    bool HasCaseStoreData();

    // the player's star balance for the Case Store (-1 = star shop off).
    // may be called from any thread
    void SetStars(int64_t stars);

    // sends pending data to the UI. call from the game's main thread
    void Tick();

    // --- the parts below are plain text processing, testable anywhere ---

    // allows melee/glove items in the trade-up context menu entries of
    // scripts/common/item_context_entries.js. returns the number of changes
    int PatchItemContextEntries(std::string &source);

    // JS array of [crateDefIndex, keyDefIndex or 0, kind] in store order
    // (weapon cases, souvenir packages, capsules, other; newest first),
    // limited to the kinds enabled in store_containers
    std::string BuildCaseStoreData(const ItemSchema &itemSchema);

    // the Case Store UI script, run in the main menu's context
    const std::string &CaseStoreScript();
}
