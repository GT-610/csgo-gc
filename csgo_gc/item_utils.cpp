#include "stdafx.h"
#include "item_utils.h"
#include "item_schema.h"

#include <algorithm>

bool GetItemPaintKitDefIndex(const CSOEconItem &item, const ItemSchema &schema, uint32_t &paintKitDefIndex)
{
    for (const CSOEconItemAttribute &attr : item.attribute())
    {
        if (attr.def_index() == ItemSchema::AttributeTexturePrefab)
        {
            paintKitDefIndex = schema.AttributeUint32(&attr);
            return true;
        }
    }

    return false;
}

std::string GetItemCollectionId(const CSOEconItem &item, const ItemSchema &schema)
{
    uint32_t paintKitDefIndex = 0;
    if (!GetItemPaintKitDefIndex(item, schema, paintKitDefIndex))
    {
        return {};
    }

    std::vector<std::string> collections;
    if (!schema.GetCollectionsForPaintedItem(item.def_index(), paintKitDefIndex, collections))
    {
        return {};
    }

    std::sort(collections.begin(), collections.end());
    return collections.front();
}

std::string GetCollectionName(const ItemSchema &schema, std::string_view collectionId)
{
    if (collectionId.empty())
    {
        return "Unknown";
    }

    return schema.GetCollectionDisplayName(collectionId);
}
