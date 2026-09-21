#include "stdafx.h"
#include "game_types.h"
#include "platform.h"

namespace GameTypes
{
namespace
{

// The interface the game exposes its mode table through. gametypes.cpp is shared
// code compiled into both client.dll and server.dll, and each registers this
// name through EXPOSE_SINGLE_INTERFACE_GLOBALVAR.
constexpr const char *GameTypesInterfaceVersion = "VENGINE_GAMETYPES_VERSION002";

// IGameTypes is a plain interface, not an IAppSystem, so vtable slot 0 is the
// virtual destructor. These are the vtable indices of GetCurrentGameType and
// GetCurrentGameMode, matching igametypes.h's declaration order and the call
// sites inside the shipped client.dll and server.dll, where they appear as the
// byte offsets 0x20 and 0x24.
constexpr size_t GetCurrentGameTypeIndex = 8;
constexpr size_t GetCurrentGameModeIndex = 9;

static_assert(GetCurrentGameModeIndex == GetCurrentGameTypeIndex + 1,
    "the two accessors are declared next to each other in igametypes.h");

using GetCurrentValueFn = int (*)(const void *);

// The vtable is an array of function pointers, so the entries are read through a
// function pointer type. Naming the array type lets the object's leading vtable
// pointer be reinterpreted in one step; casting a single entry from a const data
// pointer to a function pointer instead is rejected by Clang, because that cast
// drops the const qualifier.
using GameTypesVTable = GetCurrentValueFn const *;

// The game requests this interface from the engine's factory, but the string is
// only registered by the game modules, so try the modules that actually carry it
// as well. Failed lookups simply return null.
constexpr const char *FactoriesToTry[] = { "engine", "client", "server" };

std::mutex s_mutex;
void *s_gameTypes{};

void *ResolveLocked()
{
    using CreateInterfaceFn = void *(*)(const char *name, int *returnCode);

    for (const char *moduleName : FactoriesToTry)
    {
        void *factory = Platform::ModuleFactory(moduleName);
        if (!factory)
        {
            continue;
        }

        void *gameTypes = reinterpret_cast<CreateInterfaceFn>(factory)(GameTypesInterfaceVersion, nullptr);
        if (gameTypes)
        {
            return gameTypes;
        }
    }

    return nullptr;
}

// Returns the interface pointer, or null when it is not available yet. A failed
// lookup is not cached: the game modules may not be loaded on an early call, and
// a later call should be able to succeed.
void *GetGameTypes()
{
    std::lock_guard<std::mutex> lock{ s_mutex };

    if (!s_gameTypes)
    {
        s_gameTypes = ResolveLocked();
    }

    return s_gameTypes;
}

std::optional<int> ReadValue(size_t index)
{
    const void *gameTypes = GetGameTypes();
    if (!gameTypes)
    {
        return std::nullopt;
    }

    // The object starts with a pointer to its vtable, and the entries are indexed
    // directly because slot 0 is the virtual destructor.
    const GameTypesVTable vtable = *reinterpret_cast<const GameTypesVTable *>(gameTypes);
    if (!vtable)
    {
        return std::nullopt;
    }

    GetCurrentValueFn fn = vtable[index];
    if (!fn)
    {
        return std::nullopt;
    }

    return fn(gameTypes);
}

} // namespace

bool IsCompetitiveRuleset(int gameType, int gameMode)
{
    return gameType == GameTypeClassic
        && (gameMode == ClassicCompetitive
            || gameMode == ClassicScrimComp2v2
            || gameMode == ClassicScrimComp5v5);
}

bool IsAvailable()
{
    return ReadValue(GetCurrentGameTypeIndex).has_value();
}

int CurrentGameType()
{
    return ReadValue(GetCurrentGameTypeIndex).value_or(-1);
}

int CurrentGameMode()
{
    return ReadValue(GetCurrentGameModeIndex).value_or(-1);
}

bool IsCompetitiveRuleset()
{
    std::optional<int> gameType = ReadValue(GetCurrentGameTypeIndex);
    if (!gameType)
    {
        return false;
    }

    std::optional<int> gameMode = ReadValue(GetCurrentGameModeIndex);
    if (!gameMode)
    {
        return false;
    }

    return IsCompetitiveRuleset(*gameType, *gameMode);
}

void Reset()
{
    std::lock_guard<std::mutex> lock{ s_mutex };
    s_gameTypes = nullptr;
}

} // namespace GameTypes
