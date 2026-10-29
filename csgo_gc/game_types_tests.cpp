#include "game_types.h"

#include <cstdio>
#include <string_view>

namespace
{

// The prefix of the game's IGameTypes declaration, through the two getters.
// Let the compiler generate its native vtable and member calling convention;
// a hand-built pointer array would merely repeat the production slot constants.
class TestGameTypes
{
public:
    virtual ~TestGameTypes() = default;
    virtual bool Initialize(bool) { ++unexpectedCalls; return false; }
    virtual bool IsInitialized() const { ++unexpectedCalls; return false; }
    virtual bool SetGameTypeAndMode(const char *, const char *) { ++unexpectedCalls; return false; }
    virtual bool GetGameTypeAndModeFromAlias(const char *, int &, int &) { ++unexpectedCalls; return false; }
    virtual bool SetGameTypeAndMode(int, int) { ++unexpectedCalls; return false; }
    virtual void SetAndParseExtendedServerInfo(class KeyValues *) { ++unexpectedCalls; }
    virtual void CheckShouldSetDefaultGameModeAndType(const char *) { ++unexpectedCalls; }
    virtual int GetCurrentGameType() const { ++typeReads; return type; }
    virtual int GetCurrentGameMode() const { ++modeReads; return mode; }

    int type = GameTypes::GameTypeClassic;
    int mode = GameTypes::ClassicCompetitive;
    mutable int unexpectedCalls = 0;
    mutable int typeReads = 0;
    mutable int modeReads = 0;
};

TestGameTypes *s_testGameTypes = nullptr;

void *CreateTestInterface(const char *name, int *)
{
    return std::string_view(name) == "VENGINE_GAMETYPES_VERSION002" ? s_testGameTypes : nullptr;
}

} // namespace

namespace Platform
{

void *ModuleFactory(std::string_view module)
{
    return module == "server" ? reinterpret_cast<void *>(&CreateTestInterface) : nullptr;
}

}

static bool Check(bool expression, const char *text, int line)
{
    if (!expression)
    {
        std::printf("FAILED (line %d): %s\n", line, text);
        return false;
    }

    return true;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

namespace
{

using GameTypes::ClassicCasual;
using GameTypes::ClassicCompetitive;
using GameTypes::ClassicScrimComp2v2;
using GameTypes::ClassicScrimComp5v5;
using GameTypes::GameTypeClassic;

// Every classic mode except casual is a competitive ruleset, matching
// CCSGameRules::IsPlayingAnyCompetitiveStrictRuleset.
bool TestClassicCompetitiveModes()
{
    return CHECK(GameTypes::IsCompetitiveRuleset(GameTypeClassic, ClassicCompetitive))
        && CHECK(GameTypes::IsCompetitiveRuleset(GameTypeClassic, ClassicScrimComp2v2))
        && CHECK(GameTypes::IsCompetitiveRuleset(GameTypeClassic, ClassicScrimComp5v5));
}

bool TestCasualIsNotCompetitive()
{
    return CHECK(!GameTypes::IsCompetitiveRuleset(GameTypeClassic, ClassicCasual));
}

// The classic type is the only one with competitive rules; every other game type
// must be rejected even if the mode index happens to overlap.
bool TestOtherGameTypesAreNotCompetitive()
{
    for (int gameType = GameTypes::GameTypeGunGame; gameType <= GameTypes::GameTypeFreeForAll; gameType++)
    {
        if (!CHECK(!GameTypes::IsCompetitiveRuleset(gameType, ClassicCompetitive)))
        {
            std::printf("  game type %d was treated as competitive\n", gameType);
            return false;
        }
    }

    return true;
}

// Unknown mode indices, including ones past the end of the classic enum, must not
// be treated as competitive.
bool TestUnknownModesAreNotCompetitive()
{
    return CHECK(!GameTypes::IsCompetitiveRuleset(GameTypeClassic, 4))
        && CHECK(!GameTypes::IsCompetitiveRuleset(GameTypeClassic, -1))
        && CHECK(!GameTypes::IsCompetitiveRuleset(-1, ClassicCompetitive));
}

// Without a real game the interface cannot be read, so the queries must report
// unavailable and the gating call must fall back to the conservative answer.
bool TestUnavailableWithoutGame()
{
    GameTypes::Reset();

    return CHECK(!GameTypes::IsAvailable())
        && CHECK(!GameTypes::IsCompetitiveRuleset())
        && CHECK(GameTypes::CurrentGameType() == -1)
        && CHECK(GameTypes::CurrentGameMode() == -1);
}

// A failed lookup must not be cached: the modules may load later, and the
// interface must be resolvable again after a reset.
bool TestRepeatedUnavailableLookupsAreStable()
{
    GameTypes::Reset();

    return CHECK(!GameTypes::IsAvailable())
        && CHECK(!GameTypes::IsAvailable())
        && CHECK(!GameTypes::IsCompetitiveRuleset())
        && CHECK(!GameTypes::IsAvailable());
}

bool TestNativeInterfaceCalls()
{
    GameTypes::Reset();
    bool success = CHECK(!GameTypes::IsAvailable());

    TestGameTypes instance;
    s_testGameTypes = &instance;
    // A previous failed lookup must not prevent resolving a newly loaded module.
    success = CHECK(GameTypes::IsAvailable()) && success;
    success = CHECK(GameTypes::CurrentGameType() == GameTypes::GameTypeClassic) && success;
    success = CHECK(GameTypes::CurrentGameMode() == GameTypes::ClassicCompetitive) && success;
    success = CHECK(GameTypes::IsCompetitiveRuleset()) && success;

    // Read distinct, changing member values to detect swapped slots and a bad this
    // pointer, as well as accidental caching of the mode across map changes.
    instance.type = GameTypes::GameTypeGunGame;
    instance.mode = GameTypes::ClassicScrimComp5v5;
    success = CHECK(GameTypes::CurrentGameType() == GameTypes::GameTypeGunGame) && success;
    success = CHECK(GameTypes::CurrentGameMode() == GameTypes::ClassicScrimComp5v5) && success;
    success = CHECK(!GameTypes::IsCompetitiveRuleset()) && success;
    success = CHECK(instance.unexpectedCalls == 0) && success;
    success = CHECK(instance.typeReads > 0 && instance.modeReads > 0) && success;

    GameTypes::Reset();
    s_testGameTypes = nullptr;
    success = CHECK(!GameTypes::IsAvailable()) && success;
    return success;
}

} // namespace

int main()
{
    bool success = true;

    success = TestClassicCompetitiveModes() && success;
    success = TestCasualIsNotCompetitive() && success;
    success = TestOtherGameTypesAreNotCompetitive() && success;
    success = TestUnknownModesAreNotCompetitive() && success;
    success = TestUnavailableWithoutGame() && success;
    success = TestRepeatedUnavailableLookupsAreStable() && success;
    success = TestNativeInterfaceCalls() && success;

    if (!success)
    {
        std::printf("game_types_tests: FAILED\n");
        return 1;
    }

    std::printf("game_types_tests: all checks passed\n");
    return 0;
}
