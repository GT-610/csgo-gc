#pragma once

#include <cstdint>
#include <string_view>

class ItemSchema;

// Random popular skins for bots.
//
// Bots have no inventory, so their weapons come out of the game server plain.
// This picks a skin for a weapon from a list of well-known community favourites
// (AK-47 Redline, AWP Dragon Lore, M4A1-S Printstream, ...). Each bot gets a
// colour theme for the match (red, blue, green, purple and pink, gold and
// orange, black and white) and its weapons and knife are skins in those colours.
// Weapons a theme has no skin for get a popular one, then a random Rare-or-better
// skin that exists for the weapon. The picks are stable for one bot during a
// match and change when a new match starts.
namespace BotSkins
{
    struct Skin
    {
        uint32_t paintKit;
        uint32_t seed;
        float wear;
    };

    // resolves the skin tables against the schema. call once the schema exists
    void Build(const ItemSchema &schema);
    bool IsBuilt();

    // the next picks give different skins
    void NewMatch();

    // The bot's knife: a bot's plain starting knife ("weapon_knife", "weapon_knife_t")
    // is swapped for another knife class, e.g. "weapon_knife_karambit". nullptr
    // leaves it alone. Call before the weapon is created, then Pick for the new name.
    const char *KnifeFor(std::string_view weaponName, uint64_t botKey);

    // the colour theme of a bot for this match, e.g. "Red"
    const char *ThemeName(uint64_t botKey);

    // weaponName is the entity class name, e.g. "weapon_ak47". false when
    // there is no skin for it (knives, grenades, the bomb, ...)
    bool Pick(std::string_view weaponName, uint64_t botKey, Skin &skin);
}
