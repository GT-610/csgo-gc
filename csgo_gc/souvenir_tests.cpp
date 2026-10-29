#include "stdafx.h"
#include "item_schema.h"
#include "keyvalue.h"
#include "random.h"
#include "souvenir.h"

namespace Platform
{

void Print(const char *, ...)
{
}

}

class SouvenirTestFixture
{
public:
    explicit SouvenirTestFixture(uint32_t seed)
        : schema{ false }
        , random{ seed }
        , opening{ schema, random }
    {
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(MaxStickers); slot++)
        {
            AddAttribute(ItemSchema::AttributeStickerId0 + slot * 4, true);
            AddAttribute(ItemSchema::AttributeStickerWear0 + slot * 4, false);
            AddAttribute(ItemSchema::AttributeStickerScale0 + slot * 4, false);
            AddAttribute(ItemSchema::AttributeStickerRotation0 + slot * 4, false);
        }
    }

    bool Apply(CSOEconItem &item,
        uint32_t stickerSlotCount,
        bool dreamHack2013,
        uint32_t eventStickerKit,
        uint32_t team1StickerKit = 0,
        uint32_t team2StickerKit = 0,
        uint32_t fourthStickerKit = 0)
    {
        return opening.ApplyTournamentAttributes(item, stickerSlotCount, dreamHack2013,
            eventStickerKit, team1StickerKit, team2StickerKit, fourthStickerKit);
    }

    std::optional<uint32_t> StickerId(const CSOEconItem &item, uint32_t slot) const
    {
        const CSOEconItemAttribute *attribute = FindAttribute(item, ItemSchema::AttributeStickerId0 + slot * 4);
        return attribute ? std::optional<uint32_t>{ schema.AttributeUint32(attribute) } : std::nullopt;
    }

    std::optional<float> StickerWear(const CSOEconItem &item, uint32_t slot) const
    {
        const CSOEconItemAttribute *attribute = FindAttribute(item, ItemSchema::AttributeStickerWear0 + slot * 4);
        return attribute ? std::optional<float>{ schema.AttributeFloat(attribute) } : std::nullopt;
    }

    std::optional<float> StickerScale(const CSOEconItem &item, uint32_t slot) const
    {
        const CSOEconItemAttribute *attribute = FindAttribute(item, ItemSchema::AttributeStickerScale0 + slot * 4);
        return attribute ? std::optional<float>{ schema.AttributeFloat(attribute) } : std::nullopt;
    }

    std::optional<float> StickerRotation(const CSOEconItem &item, uint32_t slot) const
    {
        const CSOEconItemAttribute *attribute = FindAttribute(item, ItemSchema::AttributeStickerRotation0 + slot * 4);
        return attribute ? std::optional<float>{ schema.AttributeFloat(attribute) } : std::nullopt;
    }

private:
    void AddAttribute(uint32_t defIndex, bool integer)
    {
        KeyValue key{ std::to_string(defIndex) };
        if (integer)
        {
            key.AddNumber("stored_as_integer", 1);
        }

        schema.m_attributeInfo.emplace(defIndex, AttributeInfo{ key });
    }

    static const CSOEconItemAttribute *FindAttribute(const CSOEconItem &item, uint32_t defIndex)
    {
        for (const CSOEconItemAttribute &attribute : item.attribute())
        {
            if (attribute.def_index() == defIndex)
            {
                return &attribute;
            }
        }

        return nullptr;
    }

    ItemSchema schema;
    Random random;
    SouvenirOpening opening;
};

static bool SingleStickerUsesEveryAvailableSlot()
{
    uint32_t regularSlotsSeen = 0;
    uint32_t fiveSlotsSeen = 0;

    for (uint32_t seed = 0; seed < 256; seed++)
    {
        SouvenirTestFixture fixture{ seed };

        CSOEconItem regularItem;
        regularItem.set_def_index(7);
        if (!fixture.Apply(regularItem, 4, false, 101))
        {
            return false;
        }

        CSOEconItem fiveSlotItem;
        fiveSlotItem.set_def_index(11);
        if (!fixture.Apply(fiveSlotItem, 5, false, 101))
        {
            return false;
        }

        uint32_t regularStickerCount = 0;
        uint32_t fiveSlotStickerCount = 0;
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(MaxStickers); slot++)
        {
            std::optional<uint32_t> regularSticker = fixture.StickerId(regularItem, slot);
            if (regularSticker)
            {
                if (*regularSticker != 101 || slot >= 4)
                {
                    return false;
                }

                regularSlotsSeen |= 1u << slot;
                regularStickerCount++;
            }

            std::optional<uint32_t> fiveSlotSticker = fixture.StickerId(fiveSlotItem, slot);
            if (fiveSlotSticker)
            {
                if (*fiveSlotSticker != 101 || slot >= 5)
                {
                    return false;
                }

                fiveSlotsSeen |= 1u << slot;
                fiveSlotStickerCount++;
            }
        }

        if (regularStickerCount != 1 || fiveSlotStickerCount != 1)
        {
            return false;
        }
    }

    return regularSlotsSeen == 0xf && fiveSlotsSeen == 0x1f;
}

static bool MultipleStickersUseUniqueSlots()
{
    std::unordered_set<std::string> assignments;

    for (uint32_t seed = 0; seed < 64; seed++)
    {
        SouvenirTestFixture fixture{ seed };
        CSOEconItem item;
        item.set_def_index(7);

        if (!fixture.Apply(item, 4, false, 101, 102, 103, 104))
        {
            return false;
        }

        std::unordered_set<uint32_t> stickerKits;
        std::string assignment;
        for (uint32_t slot = 0; slot < 4; slot++)
        {
            std::optional<uint32_t> sticker = fixture.StickerId(item, slot);
            if (!sticker)
            {
                return false;
            }

            stickerKits.insert(*sticker);
            assignment += std::to_string(*sticker);
            assignment.push_back(',');
        }

        if (stickerKits != std::unordered_set<uint32_t>{ 101, 102, 103, 104 })
        {
            return false;
        }

        assignments.insert(std::move(assignment));
    }

    return assignments.size() > 1;
}

static bool ModernSouvenirsKeepDefaultStickerValues()
{
    SouvenirTestFixture fixture{ 1234 };
    CSOEconItem item;
    item.set_def_index(7);

    if (!fixture.Apply(item, 4, false, 101))
    {
        return false;
    }

    for (uint32_t slot = 0; slot < 4; slot++)
    {
        if (!fixture.StickerId(item, slot))
        {
            continue;
        }

        return fixture.StickerWear(item, slot) == 0.0f
            && fixture.StickerScale(item, slot) == 1.0f
            && fixture.StickerRotation(item, slot) == 0.0f;
    }

    return false;
}

static bool DreamHack2013StickerValuesAreRandomizedInRange()
{
    std::optional<float> firstWear;
    std::optional<float> firstScale;
    std::optional<float> firstRotation;
    bool wearChanged = false;
    bool scaleChanged = false;
    bool rotationChanged = false;

    for (uint32_t seed = 0; seed < 64; seed++)
    {
        SouvenirTestFixture fixture{ seed };
        CSOEconItem item;
        item.set_def_index(7);

        if (!fixture.Apply(item, 4, true, 101))
        {
            return false;
        }

        for (uint32_t slot = 0; slot < 4; slot++)
        {
            if (!fixture.StickerId(item, slot))
            {
                continue;
            }

            std::optional<float> wear = fixture.StickerWear(item, slot);
            std::optional<float> scale = fixture.StickerScale(item, slot);
            std::optional<float> rotation = fixture.StickerRotation(item, slot);
            if (!wear || !scale || !rotation
                || *wear < 0.2f || *wear > 0.3f
                || *scale < 1.0f || *scale > 1.2f
                || *rotation < -10.0f || *rotation > 10.0f)
            {
                return false;
            }

            if (!firstWear)
            {
                firstWear = wear;
                firstScale = scale;
                firstRotation = rotation;
            }
            else
            {
                wearChanged |= *wear != *firstWear;
                scaleChanged |= *scale != *firstScale;
                rotationChanged |= *rotation != *firstRotation;
            }
        }
    }

    return wearChanged && scaleChanged && rotationChanged;
}

static bool TooManyStickersFailWithoutAttributes()
{
    SouvenirTestFixture fixture{ 0 };
    CSOEconItem item;
    item.set_def_index(7);

    return !fixture.Apply(item, 3, false, 101, 102, 103, 104)
        && item.attribute_size() == 0;
}

int main()
{
    if (!SingleStickerUsesEveryAvailableSlot())
    {
        return 1;
    }

    if (!MultipleStickersUseUniqueSlots())
    {
        return 2;
    }

    if (!ModernSouvenirsKeepDefaultStickerValues())
    {
        return 3;
    }

    if (!DreamHack2013StickerValuesAreRandomizedInRange())
    {
        return 4;
    }

    if (!TooManyStickersFailWithoutAttributes())
    {
        return 5;
    }

    return 0;
}
