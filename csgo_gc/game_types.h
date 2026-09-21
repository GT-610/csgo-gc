#pragma once

// Read-only access to the game's own IGameTypes interface, the same one the
// engine and client use to answer "which game type and mode are we playing?".
//
// This is the replacement for the retired official gameserver gate. The original
// music kit StatTrak path was driven by CSGameRules::IsQueuedMatchmaking(), which
// only matched Valve matchmaking reservation servers (sv_mmqueue_reservation
// starting with 'Q'). That signal has no meaning anymore, so mode gating uses the
// game's own replicated game_type/game_mode values instead. They are
// FCVAR_REPLICATED, so a client sees the values of the server it is connected to.
//
// GetCurrentGameType and GetCurrentGameMode are the vtable indices 8 and 9 of
// IGameTypes, which is the byte offsets 0x20 and 0x24 in the shipped binaries.
namespace GameTypes
{

// CS_GameType from gametypes.h. Values match the order in GameModes.txt.
enum GameType
{
    GameTypeClassic = 0,
    GameTypeGunGame,
    GameTypeTraining,
    GameTypeCustom,
    GameTypeCooperative,
    GameTypeSkirmish,
    GameTypeFreeForAll
};

// CS_GameMode::Classic from gametypes.h.
enum ClassicGameMode
{
    ClassicCasual = 0,
    ClassicCompetitive,
    ClassicScrimComp2v2,
    ClassicScrimComp5v5
};

// Pure decision function, split out so it can be unit tested without the game.
//
// This mirrors CCSGameRules::IsPlayingAnyCompetitiveStrictRuleset(): the classic
// game type with a competitive ruleset mode. Casual and every other game type are
// excluded, while matchmaking, community and local competitive servers all
// qualify. ScrimComp5v5 and ScrimComp2v2 are included because community servers
// commonly run those modes when they host competitive rules.
bool IsCompetitiveRuleset(int gameType, int gameMode);

// The same decision using the game's current values. Returns false when the
// interface is unavailable, which is the conservative answer for callers that
// gate behavior on it.
bool IsCompetitiveRuleset();

// Whether the game type and mode could be read at all. Distinguished from
// IsCompetitiveRuleset() so a caller can tell "not competitive" apart from "we
// could not tell", for example to log the difference.
bool IsAvailable();

// Raw current game type and mode, or -1 when unavailable. For logging.
int CurrentGameType();
int CurrentGameMode();

// Forget the cached interface pointer so the next query resolves it again.
// A failed lookup is not cached in the first place, so this exists for callers
// that want to force a fresh resolution; the tests use it to put the module back
// into a known state. Safe to call at any time.
void Reset();

} // namespace GameTypes
