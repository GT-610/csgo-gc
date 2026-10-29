#include "stdafx.h"
#include "music_kit.h"
#include "game_types.h"
#include "item_schema.h"

namespace MusicKit
{

bool IsStatTrak(const AttributeValues &values)
{
    return values.hasKillEater
        && values.hasKillEaterScoreType
        && values.killEaterScoreType == ScoreTypeMusicKit;
}

bool IsEquippedMusicKit(const CSOEconItem &item, uint32_t musicKitSlot)
{
    // Music kits live in the shared no-team loadout.
    for (const CSOEconItemEquipped &equipped : item.equipped_state())
    {
        if (equipped.new_class() == 0 && equipped.new_slot() == musicKitSlot)
        {
            return true;
        }
    }

    return false;
}

std::optional<uint32_t> ReadIntegerAttribute(const CSOEconItem &item, uint32_t attributeDefIndex)
{
    for (const CSOEconItemAttribute &attribute : item.attribute())
    {
        if (attribute.def_index() != attributeDefIndex)
        {
            continue;
        }

        const std::string &bytes = attribute.value_bytes();
        if (bytes.size() != sizeof(uint32_t))
        {
            return std::nullopt;
        }

        uint32_t value;
        memcpy(&value, bytes.data(), sizeof(value));
        return value;
    }

    return std::nullopt;
}

bool WriteIntegerAttribute(CSOEconItem &item, uint32_t attributeDefIndex, uint32_t value)
{
    for (int i = 0; i < item.attribute_size(); i++)
    {
        CSOEconItemAttribute *attribute = item.mutable_attribute(i);
        if (attribute->def_index() != attributeDefIndex)
        {
            continue;
        }

        attribute->set_value_bytes(&value, sizeof(value));
        return true;
    }

    CSOEconItemAttribute *attribute = item.add_attribute();
    attribute->set_def_index(attributeDefIndex);
    attribute->set_value_bytes(&value, sizeof(value));
    return true;
}

// Decodes the music kit attributes of an item using the raw integer encoding.
//
// The ServerGC has no item schema loaded, so it cannot decode attributes through
// one. "kill eater" and "kill eater score type" are both stored_as_integer in
// items_game.txt and item creation writes them as raw 4-byte values, so reading
// the bytes directly gives the same answer. The ClientGC reads the same
// attributes through its schema; IsStatTrak() is the shared decision that keeps
// the two paths agreeing on what counts as a StatTrak music kit.
AttributeValues ReadAttributeValues(const CSOEconItem &item)
{
    AttributeValues values;

    if (std::optional<uint32_t> killEater =
            ReadIntegerAttribute(item, ItemSchema::AttributeKillEater))
    {
        values.hasKillEater = true;
        values.killEater = *killEater;
    }

    if (std::optional<uint32_t> scoreType =
            ReadIntegerAttribute(item, ItemSchema::AttributeKillEaterScoreType))
    {
        values.hasKillEaterScoreType = true;
        values.killEaterScoreType = *scoreType;
    }

    return values;
}

bool ShouldTrackStatTrak(StatTrakGate gate, int gameType, int gameMode)
{
    switch (gate)
    {
    case StatTrakGate::Always:
        return true;

    case StatTrakGate::CompetitiveRuleset:
        return GameTypes::IsCompetitiveRuleset(gameType, gameMode);
    }

    return false;
}

bool ShouldTrackStatTrak(StatTrakGate gate)
{
    if (gate == StatTrakGate::Always)
    {
        return true;
    }

    return GameTypes::IsCompetitiveRuleset();
}

} // namespace MusicKit
