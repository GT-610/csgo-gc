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
// virtual destructor. These are the offsets of GetCurrentGameType and
// GetCurrentGameMode, matching igametypes.h's declaration order and the call
// sites inside the shipped client.dll and server.dll.
constexpr size_t GetCurrentGameTypeSlot = 0x20;
constexpr size_t GetCurrentGameModeSlot = 0x24;

using GetCurrentValueFn = int (*)(const void *);

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

std::optional<int> ReadValue(size_t slot)
{
    const void *gameTypes = GetGameTypes();
    if (!gameTypes)
    {
        return std::nullopt;
    }

    // vtable[0] is the virtual destructor, so the offsets in the interface header
    // index this array directly.
    const void *const *vtable = *reinterpret_cast<const void *const *const *>(gameTypes);
    GetCurrentValueFn fn = reinterpret_cast<GetCurrentValueFn>(vtable[slot / sizeof(void *)]);
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
    return ReadValue(GetCurrentGameTypeSlot).has_value();
}

int CurrentGameType()
{
    return ReadValue(GetCurrentGameTypeSlot).value_or(-1);
}

int CurrentGameMode()
{
    return ReadValue(GetCurrentGameModeSlot).value_or(-1);
}

bool IsCompetitiveRuleset()
{
    std::optional<int> gameType = ReadValue(GetCurrentGameTypeSlot);
    if (!gameType)
    {
        return false;
    }

    std::optional<int> gameMode = ReadValue(GetCurrentGameModeSlot);
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
