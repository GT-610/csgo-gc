#pragma once

#include "config.h"

#include <string>
#include <vector>

class ItemSchema;
class CSOEconItem;

// Stars are earned from playing and spent in the Case Store: weapon cases,
// souvenir packages, capsules (each with its key) and random agents.
namespace StarShop
{
    enum class Reason : uint32_t
    {
        Kill,
        Headshot, // on top of the kill
        Mvp,
        Plant,
        Defuse,
        MatchComplete,
    };

    // stars awarded for a reason, 0 when the star shop is off
    uint32_t Reward(const StarShopConfig &config, Reason reason);

    // what one unit of a store item costs. 0 = free (keys, which come with
    // their case, and everything that isn't a case, capsule or agent)
    uint32_t Price(const ItemSchema &itemSchema, const StarShopConfig &config, uint32_t defIndex);

    bool IsAgent(const ItemSchema &itemSchema, uint32_t defIndex);

    // what an item pays when it is sold (deleted): a skin by its rarity, a knife or
    // glove a fixed amount. 0 for anything else (cases, stickers, default items...)
    uint32_t SellValue(const ItemSchema &itemSchema, const StarShopConfig &config, const CSOEconItem &item);
    // cases, souvenir packages, capsules and other crate_* containers (not keys)
    bool IsContainer(const ItemSchema &itemSchema, uint32_t defIndex);

    // the containers the star store sells: the enabled kinds, newest first. with applyLimits only the best max_*
    // of each kind (what the operation page has room for); without, every one of them
    std::vector<uint32_t> ShopContainers(const ItemSchema &itemSchema, const StarShopConfig &config, bool applyLimits);

    // the one item the star store sells in place of every agent: buying it gives a random agent, whichever agent
    // it is. 0 when the schema has no agents
    uint32_t RandomAgentItem(const ItemSchema &itemSchema);
    // the item whose picture stands for it in the shop (the agent pack), or 0 to use the item itself
    uint32_t RandomAgentPicture(const ItemSchema &itemSchema);

    // the balance the Case Store screen shows (-1 = hide it). set by the GC,
    // read by the UI hook; safe from any thread
    void SetDisplayBalance(int64_t stars);
    int64_t DisplayBalance();

    // --- the operation page ---
    // The goods listed on the operation page. The game reads them from the
    // operational_point_redeemable blocks of items_game.txt (patched from this
    // same list) and tells the GC which one was bought by its position, so the
    // GC rebuilds the identical list to know what to hand out.
    enum class GoodKind
    {
        Container, // a case, souvenir package or capsule (cases come with their key)
        Agent, // a random agent, whichever agent the page shows
    };

    struct Redeemable
    {
        GoodKind kind;
        uint32_t points; // star cost
        uint32_t defIndex; // the container, 0 for agents
        std::string itemName; // items_game name shown by the page
        uint32_t uiOrder;
    };

    std::vector<Redeemable> BuildRedeemables(const ItemSchema &itemSchema, const StarShopConfig &config);

    // the operational_point_redeemable blocks for items_game.txt
    std::string BuildRedeemablesText(const std::vector<Redeemable> &goods);
}
