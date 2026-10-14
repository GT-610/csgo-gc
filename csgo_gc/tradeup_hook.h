#pragma once

// Lets the client's trade-up screen accept 5 Coverts / 5 knives or gloves.
//
// The client only lists an item for a trade-up recipe (filter -3) when the
// item's collection has an item one rarity higher. Coverts (nothing above them
// in a collection) and knives/gloves (no collection) always fail that check,
// so the gold recipes csgo_gc adds to items_game.txt never match. This hooks
// the client's recipe input check and skips the collection check for those
// 5-item recipes only; every other recipe is untouched.
//
// Windows only. Safe to call repeatedly: it installs once client.dll is loaded,
// and does nothing (with a console message) if the client build doesn't match.
void TryInstallTradeUpHook();
