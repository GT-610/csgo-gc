#include "stdafx.h"
#include "gold_patterns.h"

#include <initializer_list>

// Community tier lists, gathered October 2026. These are not official Valve
// data; extend or correct the tables below as you see fit.
//   Karambit Case Hardened: csdb.gg/blue-gem/karambit, steamdb.com patterns
//   Karambit Marble Fade:   csdb.gg marble fade guide
//   Butterfly / Flip CH:    profilerr.net blue gem tier lists
// Only Tier 1-3 seeds are included; anything else scores 0.

namespace
{
    // knife def indices
    constexpr uint32_t KnifeFlip = 505;
    constexpr uint32_t KnifeKarambit = 507;
    constexpr uint32_t KnifeButterfly = 515;

    // paint kit def indices
    constexpr uint32_t PaintCaseHardened = 44;
    constexpr uint32_t PaintMarbleFade = 413;

    struct SeedTier
    {
        uint32_t score;
        std::initializer_list<uint32_t> seeds;
    };

    struct PatternTable
    {
        uint32_t defIndex;
        uint32_t paintKit;
        std::initializer_list<SeedTier> tiers;
    };

    const PatternTable Tables[] = {
        // Karambit | Case Hardened (blue gems)
        { KnifeKarambit, PaintCaseHardened, {
            { 4, { 387 } },
            { 3, { 442, 269 } },
            { 2, { 905 } },
        } },

        // Karambit | Marble Fade (412 = #1 max Fire & Ice, 413 = fake Fire & Ice)
        { KnifeKarambit, PaintMarbleFade, {
            { 4, { 412 } },
            { 2, { 413 } },
        } },

        // Butterfly | Case Hardened
        { KnifeButterfly, PaintCaseHardened, {
            { 3, { 398, 182, 494, 652, 838, 29 } },
            { 2, { 147, 371, 881, 721, 917, 283, 868, 891, 749, 839, 214, 919, 73, 330, 638 } },
            { 1, { 130, 809, 798, 803, 358, 664, 424, 618, 458, 928, 858, 112, 411, 629, 782,
                   902, 713, 306, 256, 964, 457, 702, 510, 407, 92, 965, 924, 913, 694, 61,
                   776, 853, 74, 522, 488, 703, 194, 248, 108, 282, 811, 335, 499, 720 } },
        } },

        // Flip Knife | Case Hardened
        { KnifeFlip, PaintCaseHardened, {
            { 3, { 151, 321, 555, 592, 661, 670 } },
            { 2, { 103, 168, 179, 363, 387, 750, 760, 823, 828, 844, 868, 955, 969 } },
            { 1, { 4, 13, 28, 112, 137, 169, 189, 209, 228, 278, 325, 429, 430, 456, 479, 526,
                   532, 577, 628, 681, 689, 690, 695, 715, 733, 768, 770, 791, 872, 878, 879,
                   887, 905, 922, 961, 996 } },
        } },
    };
}

uint32_t GoldPatterns::Score(uint32_t defIndex, uint32_t paintKitDefIndex, uint32_t seed)
{
    for (const PatternTable &table : Tables)
    {
        if (table.defIndex != defIndex || table.paintKit != paintKitDefIndex)
        {
            continue;
        }

        uint32_t best = 0;
        for (const SeedTier &tier : table.tiers)
        {
            for (uint32_t s : tier.seeds)
            {
                if (s == seed && tier.score > best)
                {
                    best = tier.score;
                }
            }
        }
        return best;
    }

    return 0;
}
