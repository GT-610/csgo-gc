#pragma once

#include <string>
#include <string_view>

class CSOEconItem;
class ItemSchema;

bool GetItemPaintKitDefIndex(const CSOEconItem &item, const ItemSchema &schema, uint32_t &paintKitDefIndex);
std::string GetItemCollectionId(const CSOEconItem &item, const ItemSchema &schema);
std::string GetCollectionName(const ItemSchema &schema, std::string_view collectionId);
