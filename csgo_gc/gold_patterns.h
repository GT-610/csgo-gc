#pragma once

#include <cstdint>

// Pattern (paint seed) rankings for knives, used by gold-for-gold trade-ups to
// bias the output toward notable patterns. 0 means an ordinary pattern; higher
// is better. Scores are only comparable for the same knife + finish.
namespace GoldPatterns
{
    constexpr uint32_t SeedMin = 0;
    constexpr uint32_t SeedMax = 1000;

    uint32_t Score(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t seed);
}
