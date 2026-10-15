#pragma once

// Gives bots random popular skins by hooking the game server.
//
// The server hands out weapons with CCSPlayer::GiveNamedItem. This hooks it,
// and when the player is a bot, writes a skin into the new weapon's networked
// fallback skin fields (the same ones CS:GO uses for items without an
// inventory entry) so every client draws it.
//
// Windows only, only runs when "bot_skins" is on in config.txt. Installs once
// server.dll is loaded, and does nothing (with a console message) if the
// server build doesn't match the one the offsets were taken from.
void TryInstallServerHook();
