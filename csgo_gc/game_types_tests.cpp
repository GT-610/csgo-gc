#include "stdafx.h"
#include "game_types.h"

#include <cstdio>

namespace Platform
{

void Print(const char *, ...)
{
}

void *ModuleFactory(std::string_view)
{
    return nullptr;
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

    if (!success)
    {
        std::printf("game_types_tests: FAILED\n");
        return 1;
    }

    std::printf("game_types_tests: all checks passed\n");
    return 0;
}
