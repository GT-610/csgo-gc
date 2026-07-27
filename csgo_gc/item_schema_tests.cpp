#include "stdafx.h"
#include "item_schema.h"
#include "keyvalue.h"

namespace Platform
{

void Print(const char *, ...)
{
}

}

class ItemSchemaTestFixture
{
public:
    ItemSchemaTestFixture()
        : schema{ false }
    {
        KeyValue tournamentEventAttribute{ "137" };
        tournamentEventAttribute.AddNumber("stored_as_integer", 1);
        schema.m_attributeInfo.emplace(
            ItemSchema::AttributeTournamentEventId,
            AttributeInfo{ tournamentEventAttribute });

        schema.m_itemInfo.reserve(2);
        schema.m_lootLists.reserve(4);
    }

    void AddTournamentStickerCapsule(uint32_t defIndex)
    {
        ItemInfo &itemInfo = schema.m_itemInfo.try_emplace(defIndex, defIndex).first->second;
        itemInfo.m_lootListName = "tournament_capsule";

        LootList &lootList = schema.m_lootLists["tournament_capsule"];
        lootList.items.emplace_back().type = LootListItemSticker;
    }

    void AddNestedSouvenirPackage(uint32_t defIndex)
    {
        ItemInfo &itemInfo = schema.m_itemInfo.try_emplace(defIndex, defIndex).first->second;
        itemInfo.m_supplyCrateSeries = 1;
        itemInfo.m_prefabs.push_back("weapon_case_souvenirpkg");

        LootList &paintedItems = schema.m_lootLists["painted_items"];
        paintedItems.items.emplace_back().type = LootListItemPaintable;

        LootList &nestedItems = schema.m_lootLists["nested_items"];
        nestedItems.subLists.push_back(&paintedItems);

        LootList &packageContents = schema.m_lootLists["package_contents"];
        packageContents.subLists.push_back(&nestedItems);

        schema.m_revolvingLootLists.try_emplace(1, packageContents);
    }

    ItemSchema schema;
};

static bool TournamentStickerCapsuleIsNotSouvenir()
{
    ItemSchemaTestFixture fixture;
    fixture.AddTournamentStickerCapsule(9000);

    CSOEconItem capsule;
    capsule.set_def_index(9000);
    capsule.set_quality(ItemSchema::QualityTournament);

    CSOEconItemAttribute *eventAttribute = capsule.add_attribute();
    eventAttribute->set_def_index(ItemSchema::AttributeTournamentEventId);
    fixture.schema.SetAttributeUint32(eventAttribute, 21);

    return !fixture.schema.IsSouvenirPackage(capsule);
}

static bool NestedPaintedLootListIsSouvenir()
{
    ItemSchemaTestFixture fixture;
    fixture.AddNestedSouvenirPackage(9001);

    CSOEconItem package;
    package.set_def_index(9001);
    package.set_quality(ItemSchema::QualityUnique);

    return fixture.schema.IsSouvenirPackage(package);
}

int main()
{
    if (!TournamentStickerCapsuleIsNotSouvenir())
    {
        return 1;
    }

    if (!NestedPaintedLootListIsSouvenir())
    {
        return 2;
    }

    return 0;
}
