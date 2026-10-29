#pragma once

#include <stdint.h>

#include <optional>

#include "base_gcmessages.pb.h"

// Shared helpers for music kit StatTrak behavior.
//
// Both the ClientGC inventory path and the ServerGC round MVP path need to answer
// the same questions about a CSOEconItem, and both need to agree on when the
// feature is active. Keeping the rules in one place stops the two paths from
// drifting apart.
namespace MusicKit
{

// Score type value that marks a music kit counter rather than a weapon counter.
// This is what item creation writes for music kits.
constexpr uint32_t ScoreTypeMusicKit = 1;

// Attribute values are read through the item schema, which knows whether an
// attribute is stored as an integer, a float or a string. That resolution lives in
// ItemSchema, so callers pass the already-decoded kill eater value in.
struct AttributeValues
{
    // Whether the item carries a "kill eater" attribute at all. Non-StatTrak
    // music kits and unrelated items do not.
    bool hasKillEater{};

    // The decoded "kill eater" value when hasKillEater is set.
    uint32_t killEater{};

    // The decoded "kill eater score type" value. Music kits use score type 1 and
    // weapons use 0, so this is what separates a music kit counter from a weapon
    // StatTrak counter.
    uint32_t killEaterScoreType{};

    // Whether the item carries a "kill eater score type" attribute.
    bool hasKillEaterScoreType{};
};

// A StatTrak music kit is one that carries a kill eater counter of score type 1.
// An item with no kill eater at all is not a StatTrak music kit, and neither is
// one whose score type belongs to weapons.
bool IsStatTrak(const AttributeValues &values);

// Whether a CSOEconItem is equipped as a music kit. Music kits live in the shared
// no-team loadout: new_class 0 and new_slot LoadoutSlotMusicKit.
bool IsEquippedMusicKit(const CSOEconItem &item, uint32_t musicKitSlot);

// Reads an attribute whose schema entry is marked "stored_as_integer".
//
// "kill eater" and "kill eater score type" are both stored as integers in
// items_game.txt, and item creation writes them as raw 4-byte values, so the
// encoding is read directly here. That keeps the ServerGC, which has no item
// schema loaded, able to read the same counters the ClientGC writes. Returns
// nullopt when the attribute is absent or not a 4-byte value.
std::optional<uint32_t> ReadIntegerAttribute(const CSOEconItem &item, uint32_t attributeDefIndex);

// Writes an attribute whose schema entry is marked "stored_as_integer".
bool WriteIntegerAttribute(CSOEconItem &item, uint32_t attributeDefIndex, uint32_t value);

// Decodes the music kit attributes of an item.
AttributeValues ReadAttributeValues(const CSOEconItem &item);

// When the StatTrak counter and its HUD display are active.
enum class StatTrakGate
{
    // Only in a competitive ruleset. This is the default and the closest surviving
    // equivalent of the retired official behavior: matchmaking, community and
    // local competitive servers all qualify, while casual and the other game types
    // do not.
    CompetitiveRuleset,

    // In every game type and mode. Useful for community servers that run casual
    // rules, and as an escape hatch if the game type and mode cannot be read.
    Always
};

// Pure decision function, split out so it can be unit tested: should the counter
// be tracked and displayed for the given gate and the given current game type and
// mode.
bool ShouldTrackStatTrak(StatTrakGate gate, int gameType, int gameMode);

// The same decision using the configured gate and the game's current values. When
// the game type and mode cannot be read this returns false for the competitive
// gate, so an unreadable mode never silently enables the feature.
bool ShouldTrackStatTrak(StatTrakGate gate);

} // namespace MusicKit
