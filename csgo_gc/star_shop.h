#pragma once

#include "config.h"

class ItemSchema;

// Stars are earned from playing and spent in the Case Store: weapon cases,
// souvenir packages, capsules (each with its key) and random agents.
namespace StarShop
{
    enum class Reason : uint32_t
    {
        Kill,
        Headshot, // on top of the kill
        Mvp,
        Plant,
        Defuse,
        MatchComplete,
    };

    // stars awarded for a reason, 0 when the star shop is off
    uint32_t Reward(const StarShopConfig &config, Reason reason);

    // what one unit of a store item costs. 0 = free (keys, which come with
    // their case, and everything that isn't a case, capsule or agent)
    uint32_t Price(const ItemSchema &itemSchema, const StarShopConfig &config, uint32_t defIndex);

    bool IsAgent(const ItemSchema &itemSchema, uint32_t defIndex);
}
