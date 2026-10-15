#include "stdafx.h"
#include "star_shop.h"
#include "item_schema.h"

uint32_t StarShop::Reward(const StarShopConfig &config, Reason reason)
{
    if (!config.enabled)
    {
        return 0;
    }

    switch (reason)
    {
    case Reason::Kill: return config.perKill;
    case Reason::Headshot: return config.perHeadshot;
    case Reason::Mvp: return config.perMvp;
    case Reason::Plant: return config.perPlant;
    case Reason::Defuse: return config.perDefuse;
    case Reason::MatchComplete: return config.perMatch;
    }

    return 0;
}

bool StarShop::IsAgent(const ItemSchema &itemSchema, uint32_t defIndex)
{
    const std::vector<uint32_t> agents = itemSchema.GetAgentDefIndexes();
    return std::binary_search(agents.begin(), agents.end(), defIndex);
}

uint32_t StarShop::Price(const ItemSchema &itemSchema, const StarShopConfig &config, uint32_t defIndex)
{
    if (!config.enabled)
    {
        return 0;
    }

    const ItemInfo *info = itemSchema.ItemInfoByDefIndex(defIndex);
    if (!info || info->m_isCoupon)
    {
        return 0;
    }

    if (IsAgent(itemSchema, defIndex))
    {
        return config.agentPrice;
    }

    // containers are named crate_* and have contents, keys never are
    const bool container = info->m_name.compare(0, 6, "crate_") == 0
        && (info->m_supplyCrateSeries || !info->m_lootListName.empty());
    if (!container)
    {
        return 0;
    }

    switch (itemSchema.GetContainerKind(defIndex))
    {
    case ItemSchema::ContainerKind::SouvenirPackage: return config.souvenirPrice;
    case ItemSchema::ContainerKind::Capsule: return config.capsulePrice;
    default: return config.casePrice;
    }
}
