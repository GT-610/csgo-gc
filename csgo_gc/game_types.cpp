#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

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

// MSVC uses one virtual destructor entry; the Itanium ABI used on Linux and
// macOS uses complete and deleting destructor entries. The getter slots must
// account for that difference (indices are relative to the object's vptr).
#if defined(_WIN32)
constexpr size_t GetCurrentGameTypeIndex = 8;
#else
constexpr size_t GetCurrentGameTypeIndex = 9;
#endif
constexpr size_t GetCurrentGameModeIndex = GetCurrentGameTypeIndex + 1;

// Windows x86 passes this in ECX, unlike a free function's stack argument.
#if defined(_WIN32) && (defined(_M_IX86) || defined(__i386__))
using GetCurrentValueFn = int (__thiscall *)(const void *);
#else
using GetCurrentValueFn = int (*)(const void *);
#endif

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

    // The object's vptr points at the first function entry, after any ABI metadata.
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
