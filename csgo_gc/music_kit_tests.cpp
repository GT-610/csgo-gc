#include "stdafx.h"
#include "game_types.h"
#include "item_schema.h"
#include "music_kit.h"

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

constexpr uint32_t MusicKitSlot = ItemSchema::LoadoutSlotMusicKit;

// Builds an item the way item creation does, with the raw 4-byte attribute
// encoding that both the client inventory and the server path read.
void AddIntegerAttribute(CSOEconItem &item, uint32_t defIndex, uint32_t value)
{
    CSOEconItemAttribute *attribute = item.add_attribute();
    attribute->set_def_index(defIndex);
    attribute->set_value_bytes(&value, sizeof(value));
}

void EquipAsMusicKit(CSOEconItem &item, uint32_t slot = MusicKitSlot)
{
    CSOEconItemEquipped *equipped = item.add_equipped_state();
    equipped->set_new_class(0);
    equipped->set_new_slot(slot);
}

CSOEconItem MakeMusicKit(bool statTrak, uint32_t count)
{
    CSOEconItem item;
    item.set_id(1234);
    item.set_def_index(ItemSchema::ItemMusicKit);
    EquipAsMusicKit(item);

    if (statTrak)
    {
        AddIntegerAttribute(item, ItemSchema::AttributeKillEater, count);
        AddIntegerAttribute(item, ItemSchema::AttributeKillEaterScoreType,
            MusicKit::ScoreTypeMusicKit);
    }

    return item;
}

bool TestStatTrakMusicKitIsRecognized()
{
    CSOEconItem item = MakeMusicKit(true, 42);

    MusicKit::AttributeValues values = MusicKit::ReadAttributeValues(item);

    return CHECK(values.hasKillEater)
        && CHECK(values.killEater == 42)
        && CHECK(values.hasKillEaterScoreType)
        && CHECK(values.killEaterScoreType == MusicKit::ScoreTypeMusicKit)
        && CHECK(MusicKit::IsStatTrak(values));
}

// A plain music kit has no kill eater at all, so it must not count.
bool TestPlainMusicKitIsNotStatTrak()
{
    CSOEconItem item = MakeMusicKit(false, 0);

    return CHECK(!MusicKit::IsStatTrak(MusicKit::ReadAttributeValues(item)));
}

// A weapon StatTrak counter uses score type 0. It must never be mistaken for a
// music kit counter even though it also carries a kill eater attribute.
bool TestWeaponScoreTypeIsNotMusicKitStatTrak()
{
    CSOEconItem item;
    item.set_id(99);
    EquipAsMusicKit(item);
    AddIntegerAttribute(item, ItemSchema::AttributeKillEater, 500);
    AddIntegerAttribute(item, ItemSchema::AttributeKillEaterScoreType, 0);

    MusicKit::AttributeValues values = MusicKit::ReadAttributeValues(item);

    return CHECK(values.hasKillEater)
        && CHECK(!MusicKit::IsStatTrak(values));
}

// A kill eater with no score type is ambiguous, so it must not be treated as a
// music kit counter.
bool TestMissingScoreTypeIsNotStatTrak()
{
    CSOEconItem item;
    item.set_id(7);
    EquipAsMusicKit(item);
    AddIntegerAttribute(item, ItemSchema::AttributeKillEater, 10);

    MusicKit::AttributeValues values = MusicKit::ReadAttributeValues(item);

    return CHECK(values.hasKillEater)
        && CHECK(!values.hasKillEaterScoreType)
        && CHECK(!MusicKit::IsStatTrak(values));
}

// Only the shared no-team music kit slot counts as equipped.
bool TestEquippedMusicKitSlotDetection()
{
    CSOEconItem equipped = MakeMusicKit(true, 1);
    if (!CHECK(MusicKit::IsEquippedMusicKit(equipped, MusicKitSlot)))
    {
        return false;
    }

    // Same item in a different slot is not a music kit equip.
    CSOEconItem otherSlot = MakeMusicKit(true, 1);
    otherSlot.clear_equipped_state();
    EquipAsMusicKit(otherSlot, ItemSchema::LoadoutSlotGraffiti);
    if (!CHECK(!MusicKit::IsEquippedMusicKit(otherSlot, MusicKitSlot)))
    {
        return false;
    }

    // A team-specific equip is not the shared no-team loadout.
    CSOEconItem teamEquipped;
    teamEquipped.set_id(5);
    CSOEconItemEquipped *teamState = teamEquipped.add_equipped_state();
    teamState->set_new_class(3);
    teamState->set_new_slot(MusicKitSlot);

    if (!CHECK(!MusicKit::IsEquippedMusicKit(teamEquipped, MusicKitSlot)))
    {
        return false;
    }

    // An item with no equipped state at all is not equipped.
    CSOEconItem unequipped;
    unequipped.set_id(6);

    return CHECK(!MusicKit::IsEquippedMusicKit(unequipped, MusicKitSlot));
}

// The raw integer encoding is shared so the schema-less server path reads the
// same value the client wrote.
bool TestIntegerAttributeRoundTrip()
{
    CSOEconItem item;
    item.set_id(1);

    if (!CHECK(!MusicKit::ReadIntegerAttribute(item, ItemSchema::AttributeKillEater)))
    {
        return false;
    }

    if (!CHECK(MusicKit::WriteIntegerAttribute(item, ItemSchema::AttributeKillEater, 12345)))
    {
        return false;
    }

    if (!CHECK(MusicKit::ReadIntegerAttribute(item, ItemSchema::AttributeKillEater) == 12345))
    {
        return false;
    }

    // Writing again replaces rather than appending.
    if (!CHECK(MusicKit::WriteIntegerAttribute(item, ItemSchema::AttributeKillEater, 999)))
    {
        return false;
    }

    return CHECK(item.attribute_size() == 1)
        && CHECK(MusicKit::ReadIntegerAttribute(item, ItemSchema::AttributeKillEater) == 999);
}

// An attribute stored in a non-integer width must be rejected rather than
// reinterpreted as a bogus counter.
bool TestWrongWidthAttributeIsRejected()
{
    CSOEconItem item;
    item.set_id(1);

    CSOEconItemAttribute *attribute = item.add_attribute();
    attribute->set_def_index(ItemSchema::AttributeKillEater);
    attribute->set_value_bytes("\x01", 1);

    return CHECK(!MusicKit::ReadIntegerAttribute(item, ItemSchema::AttributeKillEater));
}

// The gate is the whole point of the feature: competitive rulesets only by
// default, everything when explicitly configured.
bool TestCompetitiveGate()
{
    using MusicKit::StatTrakGate;

    // Classic + competitive rulesets are allowed.
    if (!CHECK(MusicKit::ShouldTrackStatTrak(StatTrakGate::CompetitiveRuleset,
            GameTypes::GameTypeClassic, GameTypes::ClassicCompetitive)))
    {
        return false;
    }

    // Casual classic is not a competitive ruleset.
    if (!CHECK(!MusicKit::ShouldTrackStatTrak(StatTrakGate::CompetitiveRuleset,
            GameTypes::GameTypeClassic, GameTypes::ClassicCasual)))
    {
        return false;
    }

    // Other game types are not competitive rulesets even on a matching index.
    for (int gameType = GameTypes::GameTypeGunGame;
        gameType <= GameTypes::GameTypeFreeForAll; gameType++)
    {
        if (!CHECK(!MusicKit::ShouldTrackStatTrak(StatTrakGate::CompetitiveRuleset,
                gameType, GameTypes::ClassicCompetitive)))
        {
            std::printf("  game type %d wrongly allowed\n", gameType);
            return false;
        }
    }

    // The always gate ignores the mode entirely.
    return CHECK(MusicKit::ShouldTrackStatTrak(StatTrakGate::Always,
            GameTypes::GameTypeClassic, GameTypes::ClassicCasual))
        && CHECK(MusicKit::ShouldTrackStatTrak(StatTrakGate::Always,
            GameTypes::GameTypeFreeForAll, 0));
}

// Without the game, the competitive gate must refuse rather than assume yes.
bool TestCompetitiveGateFailsClosedWithoutGame()
{
    GameTypes::Reset();

    return CHECK(!MusicKit::ShouldTrackStatTrak(MusicKit::StatTrakGate::CompetitiveRuleset))
        && CHECK(MusicKit::ShouldTrackStatTrak(MusicKit::StatTrakGate::Always));
}

} // namespace

int main()
{
    bool success = true;

    success = TestStatTrakMusicKitIsRecognized() && success;
    success = TestPlainMusicKitIsNotStatTrak() && success;
    success = TestWeaponScoreTypeIsNotMusicKitStatTrak() && success;
    success = TestMissingScoreTypeIsNotStatTrak() && success;
    success = TestEquippedMusicKitSlotDetection() && success;
    success = TestIntegerAttributeRoundTrip() && success;
    success = TestWrongWidthAttributeIsRejected() && success;
    success = TestCompetitiveGate() && success;
    success = TestCompetitiveGateFailsClosedWithoutGame() && success;

    if (!success)
    {
        std::printf("music_kit_tests: FAILED\n");
        return 1;
    }

    std::printf("music_kit_tests: all checks passed\n");
    return 0;
}
