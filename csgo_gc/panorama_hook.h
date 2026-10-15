#pragma once

#include <string>

class ItemSchema;

// Small patches to the game's Panorama UI, done in memory while the game runs.
//
// The UI scripts live in the signed csgo/panorama/code.pbin, so they can't be
// edited on disk. Instead this hooks the UI engine's RunScript (every script
// in a layout file goes through it) and:
//   - adds a "Sell for N stars" entry to the inventory's right-click menu
//   - adds the SkinStop button to the main menu: one big searchable screen
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

    // sends pending data to the UI. call from the game's main thread
    void Tick();

    // --- the parts below are plain text processing, testable anywhere ---

    // the sell prices the right-click "Sell" entry shows, by rarity 1..6 (Consumer to Covert) and for knives and gloves
    struct SellPrices
    {
        uint32_t byRarity[7]{}; // index 0 unused
        uint32_t knifeOrGlove{};
    };

    // adds a "Sell for N stars" entry to item_context_entries.js, before the delete entry. selling is what the
    // game's own delete does once the star shop's selling is on: the GC pays for the item it removes.
    // returns false when the delete entry isn't found
    bool AddSellEntry(std::string &source, const SellPrices &prices);

    // makes the contents list of a case (popup_capability_decodable.js) load each skin's textures before it shows
    // the skin's picture. the game only does that for the one you click, so a skin and weapon pair it has no
    // ready-made picture of (every one in a custom case) stays a blank tile. returns the number of changes
    int PatchCasePopup(std::string &source);

    // lets a context menu entry whose name starts with '!' show that text as it is (the menu would look the
    // name up as a language token). returns the number of changes
    int PatchContextMenuLabels(std::string &source);

    // JS array of [crateDefIndex, keyDefIndex or 0, kind] in store order
    // (weapon cases, souvenir packages, capsules, other; newest first),
    // limited to the kinds enabled in store_containers
    std::string BuildCaseStoreData(const ItemSchema &itemSchema);

    // the Case Store UI script, run in the main menu's context
    const std::string &CaseStoreScript();
}
