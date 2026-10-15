#include "stdafx.h"
#include "star_shop.h"
#include "item_schema.h"
#include "config.h"

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

static std::atomic<int64_t> s_displayBalance{ -1 };

void StarShop::SetDisplayBalance(int64_t stars)
{
    s_displayBalance = stars;
}

int64_t StarShop::DisplayBalance()
{
    return s_displayBalance;
}

bool StarShop::IsAgent(const ItemSchema &itemSchema, uint32_t defIndex)
{
    const std::vector<uint32_t> agents = itemSchema.GetAgentDefIndexes();
    return std::binary_search(agents.begin(), agents.end(), defIndex);
}

bool StarShop::IsContainer(const ItemSchema &itemSchema, uint32_t defIndex)
{
    const ItemInfo *info = itemSchema.ItemInfoByDefIndex(defIndex);
    return info && !info->m_isCoupon && info->m_name.compare(0, 6, "crate_") == 0
        && (info->m_supplyCrateSeries || !info->m_lootListName.empty());
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

    if (!IsContainer(itemSchema, defIndex))
    {
        return 0;
    }

    if (itemSchema.IsCustomCase(defIndex))
    {
        return config.customCasePrice;
    }

    switch (itemSchema.GetContainerKind(defIndex))
    {
    case ItemSchema::ContainerKind::SouvenirPackage: return config.souvenirPrice;
    case ItemSchema::ContainerKind::Capsule: return config.capsulePrice;
    default: return config.casePrice;
    }
}

std::vector<uint32_t> StarShop::ShopContainers(const ItemSchema &itemSchema, const StarShopConfig &config)
{
    using Kind = ItemSchema::ContainerKind;
    const GCConfig &gcConfig = GetConfig();
    auto kindEnabled = [&](Kind kind)
    {
        switch (kind)
        {
        case Kind::WeaponCase: return gcConfig.StoreWeaponCases();
        case Kind::SouvenirPackage: return gcConfig.StoreSouvenirPackages();
        case Kind::Capsule: return gcConfig.StoreCapsules();
        default: return gcConfig.StoreOtherContainers();
        }
    };

    auto limitFor = [&](Kind kind) -> uint32_t
    {
        switch (kind)
        {
        case Kind::WeaponCase: return config.maxCases;
        case Kind::SouvenirPackage: return config.maxSouvenirPackages;
        case Kind::Capsule: return config.maxCapsules;
        default: return 0;
        }
    };

    // weapon cases, souvenir packages, capsules, other; newest first
    std::vector<uint32_t> crates = itemSchema.GetContainerDefIndexes();
    std::stable_sort(crates.begin(), crates.end(), [&](uint32_t a, uint32_t b)
    {
        const Kind kindA = itemSchema.GetContainerKind(a);
        const Kind kindB = itemSchema.GetContainerKind(b);
        if (kindA != kindB)
        {
            return kindA < kindB;
        }
        return a > b;
    });

    // custom cases come first, whatever the limits and kinds, and aren't
    // counted in them
    std::vector<uint32_t> selected;
    for (uint32_t crate : itemSchema.GetCustomCaseDefIndexes())
    {
        const ItemInfo *info = itemSchema.ItemInfoByDefIndex(crate);
        if (info && !info->m_name.empty() && itemSchema.CanCreateItem(crate))
        {
            selected.push_back(crate);
        }
    }

    std::array<uint32_t, 4> taken{};
    for (uint32_t crate : crates)
    {
        if (itemSchema.IsCustomCase(crate))
        {
            continue;
        }

        const Kind kind = itemSchema.GetContainerKind(crate);
        const ItemInfo *info = itemSchema.ItemInfoByDefIndex(crate);
        if (!info || info->m_name.empty() || !kindEnabled(kind)
            || info->m_name.find('"') != std::string::npos)
        {
            continue;
        }

        const uint32_t limit = limitFor(kind);
        uint32_t &count = taken[static_cast<size_t>(kind)];
        if (limit && count >= limit)
        {
            continue;
        }

        ++count;
        selected.push_back(crate);
    }

    return selected;
}

std::vector<StarShop::Redeemable> StarShop::BuildRedeemables(const ItemSchema &itemSchema,
    const StarShopConfig &config)
{
    std::vector<Redeemable> goods;
    if (!config.enabled)
    {
        return goods;
    }

    // The page may list goods by ui_order (agents 1, cases and packages 2,
    // capsules 3). Keeping this list sorted that way means a good's position is
    // the same whether the client counts in file order or in display order.
    //
    // One random agent. The page needs a real item to show, so it borrows an
    // agent pack's name and picture; whichever it is, the GC hands out any agent.
    if (!itemSchema.GetAgentDefIndexes().empty())
    {
        std::string displayName;
        if (const ItemInfo *pack = itemSchema.ItemInfoByName("character_operator_dossier_op11_legendary"))
        {
            displayName = pack->m_name;
        }
        else if (const ItemInfo *agent = itemSchema.ItemInfoByDefIndex(itemSchema.GetAgentDefIndexes().front()))
        {
            displayName = agent->m_name;
        }

        if (!displayName.empty())
        {
            goods.push_back({ GoodKind::Agent, config.agentPrice, 0, displayName, 1 });
        }
    }

    using Kind = ItemSchema::ContainerKind;
    std::vector<Redeemable> containers;
    for (uint32_t crate : ShopContainers(itemSchema, config))
    {
        const Kind kind = itemSchema.GetContainerKind(crate);
        const ItemInfo *info = itemSchema.ItemInfoByDefIndex(crate);
        containers.push_back({ GoodKind::Container, Price(itemSchema, config, crate), crate,
            info->m_name, kind == Kind::Capsule ? 3u : 2u });
    }
    std::stable_sort(containers.begin(), containers.end(),
        [](const Redeemable &a, const Redeemable &b) { return a.uiOrder < b.uiOrder; });
    goods.insert(goods.end(), containers.begin(), containers.end());

    return goods;
}

std::string StarShop::BuildRedeemablesText(const std::vector<Redeemable> &goods)
{
    std::string text;
    for (const Redeemable &good : goods)
    {
        text += "\t\t\t\"operational_point_redeemable\"\n";
        text += "\t\t\t{\n";
        text += "\t\t\t\t\"points\"\t\t\"" + std::to_string(good.points) + "\"\n";
        text += "\t\t\t\t\"item_name\"\t\t\"" + good.itemName + "\"\n";
        text += "\t\t\t\t\"ui_order\"\t\t\"" + std::to_string(good.uiOrder) + "\"\n";
        text += "\t\t\t}\n";
    }
    return text;
}
