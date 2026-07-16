#include "stdafx.h"
#include "gc_client.h"
#include "graffiti.h"
#include "item_utils.h"
#include "keyvalue.h"
#include "networking_shared.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>

namespace
{

struct RconCommand
{
    explicit RconCommand(std::string command_)
        : command{ std::move(command_) }
    {
    }

    std::string command;
    std::promise<std::string> response;
};

using RconCommandHolder = std::shared_ptr<RconCommand>;

constexpr size_t SourceRconResponseBodyLimit = 4096 - 10;

template<typename T>
bool TryParseNumber(std::string_view text, T &value)
{
    T parsed{};
    auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
    {
        return false;
    }

    value = parsed;
    return true;
}

bool TryParseFloat01(std::string_view text, float &value)
{
    std::string input{ text };
    char *end = nullptr;
    errno = 0;
    float parsed = std::strtof(input.c_str(), &end);
    if (input.empty() || errno == ERANGE || end != input.c_str() + input.size() || !std::isfinite(parsed))
    {
        return false;
    }

    value = parsed;
    return value >= 0.0f && value <= 1.0f;
}

bool TryParseFloat(std::string_view text, float &value)
{
    std::string input{ text };
    char *end = nullptr;
    errno = 0;
    float parsed = std::strtof(input.c_str(), &end);
    if (input.empty() || errno == ERANGE || end != input.c_str() + input.size() || !std::isfinite(parsed))
    {
        return false;
    }

    value = parsed;
    return true;
}

bool IsValidQuality(uint32_t quality)
{
    return quality <= ItemSchema::QualityTournament;
}

bool IsValidRarity(uint32_t rarity)
{
    return rarity <= ItemSchema::RarityImmortal || rarity == ItemSchema::RarityUnusual;
}

std::string ToLower(std::string value)
{
    for (char &ch : value)
    {
        ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    }

    return value;
}

bool TokenizeRconCommand(std::string_view command, std::vector<std::string> &tokens)
{
    size_t i = 0;
    while (i < command.size())
    {
        while (i < command.size() && command[i] <= ' ')
        {
            i++;
        }

        if (i >= command.size())
        {
            break;
        }

        std::string token;
        bool quoted = false;
        for (; i < command.size(); i++)
        {
            char ch = command[i];
            if (quoted)
            {
                if (ch == '\\' && i + 1 < command.size())
                {
                    token.push_back(command[++i]);
                    continue;
                }

                if (ch == '"')
                {
                    quoted = false;
                    continue;
                }

                token.push_back(ch);
                continue;
            }

            if (ch <= ' ')
            {
                break;
            }

            if (ch == '"')
            {
                quoted = true;
                continue;
            }

            token.push_back(ch);
        }

        if (quoted)
        {
            return false;
        }

        tokens.push_back(std::move(token));
    }

    return true;
}

std::string QuoteRconValue(std::string_view value)
{
    std::string result{ "\"" };
    for (char ch : value)
    {
        if (ch == '\\' || ch == '"')
        {
            result.push_back('\\');
        }
        result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

std::string ItemName(const ItemSchema &schema, const CSOEconItem &item)
{
    const ItemInfo *itemInfo = schema.ItemInfoByDefIndex(item.def_index());
    if (itemInfo && !itemInfo->m_name.empty())
    {
        return itemInfo->m_name;
    }

    return "Unknown Item";
}

std::string ItemSummary(const ItemSchema &schema, const CSOEconItem &item)
{
    std::ostringstream response;
    response << "item id=" << item.id()
             << " defindex=" << item.def_index()
             << " name=" << QuoteRconValue(ItemName(schema, item))
             << " quality=" << item.quality()
             << " rarity=" << item.rarity()
             << " level=" << item.level();

    if (!item.custom_name().empty())
    {
        response << " custom_name=" << QuoteRconValue(item.custom_name());
    }

    return response.str();
}

std::string EquippedSummary(const CSOEconItem &item)
{
    if (item.equipped_state_size() == 0)
    {
        return "none";
    }

    std::ostringstream response;
    for (int i = 0; i < item.equipped_state_size(); i++)
    {
        const CSOEconItemEquipped &equipped = item.equipped_state(i);
        if (i)
        {
            response << ",";
        }
        response << equipped.new_class() << ":" << equipped.new_slot();
    }
    return response.str();
}

bool ItemMatchesText(const ItemSchema &schema, const CSOEconItem &item, std::string_view query)
{
    std::string loweredQuery = ToLower(std::string{ query });
    std::string loweredName = ToLower(ItemName(schema, item));
    std::string loweredCustomName = ToLower(item.custom_name());

    return loweredName.find(loweredQuery) != std::string::npos
        || loweredCustomName.find(loweredQuery) != std::string::npos;
}

std::vector<const CSOEconItem *> SortedInventoryItems(const Inventory &inventory)
{
    std::vector<const CSOEconItem *> items;
    items.reserve(inventory.ItemCount());

    for (const auto &pair : inventory.Items())
    {
        items.push_back(&pair.second);
    }

    std::sort(items.begin(), items.end(), [](const CSOEconItem *left, const CSOEconItem *right) {
        return left->id() < right->id();
    });

    return items;
}

template<typename HeaderFor, typename LineFor>
std::string BuildLimitedRconLines(size_t lineCount, HeaderFor headerFor, LineFor lineFor)
{
    std::vector<std::string> lines;
    lines.reserve(lineCount);

    size_t lineBytes = 0;
    bool truncated = false;

    for (size_t i = 0; i < lineCount; i++)
    {
        std::string line = lineFor(i);
        std::string header = headerFor(lines.size() + 1, false);
        size_t nextLineBytes = lineBytes + 1 + line.size();
        if (header.size() + nextLineBytes > SourceRconResponseBodyLimit)
        {
            truncated = true;
            break;
        }

        lineBytes = nextLineBytes;
        lines.push_back(std::move(line));
    }

    std::string response = headerFor(lines.size(), truncated);
    response.reserve(response.size() + lineBytes);
    for (const std::string &line : lines)
    {
        response.push_back('\n');
        response.append(line);
    }

    return response;
}

std::string Usage(std::string_view usage)
{
    std::string response{ "ERR usage: " };
    response.append(usage.data(), usage.size());
    return response;
}

std::string InvalidParameter(std::string_view key)
{
    std::string response{ "ERR invalid parameter " };
    response.append(key.data(), key.size());
    return response;
}

std::string UnknownParameter(std::string_view key)
{
    std::string response{ "ERR unknown parameter " };
    response.append(key.data(), key.size());
    return response;
}

} // namespace

ClientGC::ClientGC(uint64_t steamId)
    : m_steamId{ steamId }
    , m_inventory{ steamId }
{
    // also called from ServerGC's constructor
    Graffiti::Initialize();

    // The inventory exists before the worker thread starts, so seed the
    // round_mvp event cache here and keep later refreshes on the worker thread.
    if (m_inventory.EquippedMusicKitItemId(true))
    {
        m_cachedMusicKitMVPs.store(static_cast<int32_t>(m_inventory.EquippedMusicKitMVPCount(false)));
    }

    StartThread();

    Platform::Print("ClientGC spawned for user %llu\n", steamId);
}

ClientGC::~ClientGC()
{
    StopThread();
    Platform::Print("ClientGC destroyed\n");
}

uint32_t ClientGC::LocalPlayerMusicKitMVPsForRoundMVPEvent() const
{
    // round_mvp is observed before the worker-thread inventory mirror applies
    // the local increment, so expose the post-MVP value here.
    int32_t cachedMVPs = m_cachedMusicKitMVPs.load();
    return cachedMVPs >= 0 ? static_cast<uint32_t>(cachedMVPs + 1) : 0;
}

std::string ClientGC::RunRconCommand(std::string command)
{
    auto request = std::make_shared<RconCommand>(std::move(command));
    std::future<std::string> response = request->response.get_future();

    auto holder = std::make_unique<RconCommandHolder>(request);
    RconCommandHolder *rawHolder = holder.get();
    PostToGC(GCEvent::RconCommand, 0, &rawHolder, sizeof(rawHolder));
    holder.release();

    if (response.wait_for(std::chrono::seconds{ 5 }) != std::future_status::ready)
    {
        return "ERR timeout";
    }

    return response.get();
}

void ClientGC::RefreshCachedMusicKitMVPs()
{
    if (!m_inventory.EquippedMusicKitItemId(true))
    {
        m_cachedMusicKitMVPs.store(-1);
        SendMusicKitMVPStateToGameServer();
        return;
    }

    m_cachedMusicKitMVPs.store(static_cast<int32_t>(m_inventory.EquippedMusicKitMVPCount(false)));
    SendMusicKitMVPStateToGameServer();
}

void ClientGC::SendMusicKitMVPStateToGameServer()
{
    int32_t userId = m_localUserId.load();
    if (userId <= 0)
    {
        return;
    }

    GCMessageWrite messageWrite{ k_EMsgNetworkMusicKitMVPState };

    int32_t cachedMVPs = m_cachedMusicKitMVPs.load();
    uint32_t currentMVPs = cachedMVPs >= 0 ? static_cast<uint32_t>(cachedMVPs) : 0;
    uint32_t hasEquippedStatTrakMusicKit = cachedMVPs >= 0 ? 1u : 0u;

    messageWrite.WriteUint32(static_cast<uint32_t>(userId));
    messageWrite.WriteUint32(hasEquippedStatTrakMusicKit);
    messageWrite.WriteUint32(currentMVPs);

    Platform::Print("ClientGC: syncing music kit MVP state to server: userid=%d haskit=%u mvps=%u\n",
        userId,
        hasEquippedStatTrakMusicKit,
        currentMVPs);
    PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
}

void ClientGC::SyncLocalPlayerMusicKitState(int userId)
{
    if (userId <= 0)
    {
        return;
    }

    int32_t previousUserId = m_localUserId.exchange(userId);
    if (previousUserId != userId)
    {
        Platform::Print("ClientGC: local userid changed from %d to %d, syncing music kit state\n",
            previousUserId,
            userId);
        SendMusicKitMVPStateToGameServer();
    }
}

void ClientGC::HandleEvent(GCEvent type, uint64_t id, const std::vector<uint8_t> &buffer)
{
    switch (type)
    {
    case GCEvent::Message:
        HandleMessage(static_cast<uint32_t>(id), buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::NetMessage:
        HandleNetMessage(buffer.data(), static_cast<uint32_t>(buffer.size()));
        break;

    case GCEvent::SOCacheRequest:
        HandleSOCacheRequest();
        break;

    case GCEvent::LocalPlayerRoundMVP:
        LocalPlayerRoundMVP();
        break;

    case GCEvent::SyncLocalPlayerMusicKitState:
        if (buffer.size() == sizeof(uint32_t))
        {
            uint32_t userId;
            memcpy(&userId, buffer.data(), sizeof(userId));
            SyncLocalPlayerMusicKitState(static_cast<int>(userId));
        }
        else
        {
            Platform::Print("ClientGC::HandleEvent: invalid SyncLocalPlayerMusicKitState buffer size\n");
        }
        break;

    case GCEvent::RconCommand:
        if (buffer.size() == sizeof(RconCommandHolder *))
        {
            RconCommandHolder *rawHolder;
            memcpy(&rawHolder, buffer.data(), sizeof(rawHolder));

            std::unique_ptr<RconCommandHolder> holder{ rawHolder };
            RconCommandHolder request = std::move(*holder);

            request->response.set_value(ExecuteRconCommand(request->command));
        }
        else
        {
            Platform::Print("ClientGC::HandleEvent: invalid RconCommand buffer size\n");
        }
        break;

    default:
        Platform::Print("ClientGC::HandleEvent: unknown event type %d\n", static_cast<int>(type));
        break;
    }
}

const ClientGC::RconCommandDef *ClientGC::RconCommands(size_t &count)
{
    static const RconCommandDef Commands[]{
        { "help", "help", &ClientGC::RconHelp },
        { "ping", "ping", &ClientGC::RconPing },
        { "status", "status", &ClientGC::RconStatus },
        { "clients", "clients", &ClientGC::RconClients },
        { "list_items", "list_items [limit]", &ClientGC::RconListItems },
        { "find_item", "find_item <itemid|defindex|text>", &ClientGC::RconFindItem },
        { "item_info", "item_info <itemid>", &ClientGC::RconItemInfo },
        { "give_item", "give_item <defindex> [count] [key=value...]", &ClientGC::RconGiveItem },
        { "remove_item", "remove_item <itemid>", &ClientGC::RconRemoveItem },
        { "refresh_inventory", "refresh_inventory", &ClientGC::RconRefreshInventory },
        { "save_inventory", "save_inventory", &ClientGC::RconSaveInventory },
    };

    count = sizeof(Commands) / sizeof(Commands[0]);
    return Commands;
}

std::string ClientGC::RconCommandUsageList()
{
    std::ostringstream response;

    size_t commandCount = 0;
    const RconCommandDef *commands = RconCommands(commandCount);
    for (size_t i = 0; i < commandCount; i++)
    {
        response << (i ? ", " : "") << commands[i].usage;
    }

    return response.str();
}

std::string ClientGC::ExecuteRconCommand(std::string_view command)
{
    RconRequest request;
    std::vector<std::string> tokens;
    if (!TokenizeRconCommand(command, tokens))
    {
        return "ERR malformed command";
    }

    if (tokens.empty())
    {
        return "ERR unknown command";
    }

    request.name = std::move(tokens[0]);
    request.name = ToLower(std::move(request.name));
    for (size_t i = 1; i < tokens.size(); i++)
    {
        request.args.push_back(std::move(tokens[i]));
    }

    size_t commandCount = 0;
    const RconCommandDef *commands = RconCommands(commandCount);
    for (size_t i = 0; i < commandCount; i++)
    {
        const RconCommandDef &commandDef = commands[i];
        if (request.name == commandDef.name)
        {
            return (this->*commandDef.handler)(request);
        }
    }

    return "ERR unknown command";
}

std::string ClientGC::RconHelp(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("help");
    }

    return "OK commands: " + RconCommandUsageList();
}

std::string ClientGC::RconPing(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("ping");
    }

    return "OK pong";
}

std::string ClientGC::RconStatus(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("status");
    }

    std::ostringstream response;
    response << "OK rcon=enabled client=online steamid=" << m_steamId
             << " items=" << m_inventory.ItemCount();
    return response.str();
}

std::string ClientGC::RconClients(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("clients");
    }

    std::ostringstream response;
    response << "OK steamid=" << m_steamId;
    return response.str();
}

std::string ClientGC::RconListItems(const RconRequest &request)
{
    if (request.args.size() > 1)
    {
        return Usage("list_items [limit]");
    }

    uint32_t limit = 50;
    if (!request.args.empty() && (!TryParseNumber(request.args[0], limit) || limit == 0 || limit > 500))
    {
        return Usage("list_items [limit]");
    }

    std::vector<const CSOEconItem *> items = SortedInventoryItems(m_inventory);
    const ItemSchema &schema = m_inventory.GetItemSchema();

    size_t maxShown = std::min<size_t>(limit, items.size());
    return BuildLimitedRconLines(maxShown,
        [&](size_t shown, bool truncated) {
            std::ostringstream response;
            response << "OK total=" << items.size()
                     << " shown=" << shown
                     << " truncated=" << (truncated ? 1 : 0);
            return response.str();
        },
        [&](size_t index) {
            return ItemSummary(schema, *items[index]);
        });
}

std::string ClientGC::RconFindItem(const RconRequest &request)
{
    if (request.args.size() != 1)
    {
        return Usage("find_item <itemid|defindex|text>");
    }

    std::vector<const CSOEconItem *> matches;
    const ItemSchema &schema = m_inventory.GetItemSchema();

    uint64_t numericQuery = 0;
    bool numeric = TryParseNumber(request.args[0], numericQuery);
    for (const CSOEconItem *item : SortedInventoryItems(m_inventory))
    {
        if (numeric)
        {
            if (item->id() == numericQuery || item->def_index() == numericQuery)
            {
                matches.push_back(item);
            }
        }
        else if (ItemMatchesText(schema, *item, request.args[0]))
        {
            matches.push_back(item);
        }
    }

    constexpr size_t MaxShown = 50;
    size_t maxShown = std::min(MaxShown, matches.size());
    return BuildLimitedRconLines(maxShown,
        [&](size_t shown, bool truncated) {
            std::ostringstream response;
            response << "OK total=" << matches.size()
                     << " shown=" << shown
                     << " truncated=" << (truncated ? 1 : 0);
            return response.str();
        },
        [&](size_t index) {
            return ItemSummary(schema, *matches[index]);
        });
}

std::string ClientGC::RconItemInfo(const RconRequest &request)
{
    if (request.args.size() != 1)
    {
        return Usage("item_info <itemid>");
    }

    uint64_t itemId;
    if (!TryParseNumber(request.args[0], itemId))
    {
        return Usage("item_info <itemid>");
    }

    const CSOEconItem *item = m_inventory.GetItem(itemId);
    if (!item)
    {
        return "ERR item not found";
    }

    const ItemSchema &schema = m_inventory.GetItemSchema();
    auto header = [&](size_t shown, bool truncated) {
        std::ostringstream response;
        response << "OK " << ItemSummary(schema, *item)
                 << " inventory=" << item->inventory()
                 << " origin=" << item->origin()
                 << " flags=" << item->flags()
                 << " in_use=" << item->in_use()
                 << " equipped=" << EquippedSummary(*item)
                 << " attributes=" << item->attribute_size()
                 << " attr_shown=" << shown
                 << " truncated=" << (truncated ? 1 : 0);
        return response.str();
    };

    return BuildLimitedRconLines(static_cast<size_t>(item->attribute_size()),
        header,
        [&](size_t index) {
            const CSOEconItemAttribute &attribute = item->attribute(static_cast<int>(index));
            std::ostringstream line;
            line << "attr defindex=" << attribute.def_index()
                 << " value=" << QuoteRconValue(schema.AttributeString(&attribute));
            return line.str();
        });
}

std::string ClientGC::RconGiveItem(const RconRequest &request)
{
    constexpr const char *GiveItemUsage = "give_item <defindex> [count] [key=value...]";

    if (request.args.empty())
    {
        return Usage(GiveItemUsage);
    }

    uint32_t defIndex;
    if (!TryParseNumber(request.args[0], defIndex))
    {
        return Usage(GiveItemUsage);
    }

    uint32_t count = 1;
    size_t parameterStart = 1;
    if (parameterStart < request.args.size() && request.args[parameterStart].find('=') == std::string::npos)
    {
        if (!TryParseNumber(request.args[parameterStart], count) || count == 0 || count > 100)
        {
            return Usage(GiveItemUsage);
        }
        parameterStart++;
    }

    Inventory::ParameterizedItemOptions options;
    bool hasParameters = parameterStart < request.args.size();

    for (size_t i = parameterStart; i < request.args.size(); i++)
    {
        std::string_view parameter{ request.args[i] };
        size_t separator = parameter.find('=');
        if (separator == std::string_view::npos || separator == 0)
        {
            return Usage(GiveItemUsage);
        }

        std::string key = ToLower(std::string{ parameter.substr(0, separator) });
        std::string_view value = parameter.substr(separator + 1);

        auto parseUint32 = [&](std::optional<uint32_t> &target) -> std::optional<std::string> {
            uint32_t parsed;
            if (!TryParseNumber(value, parsed))
            {
                return InvalidParameter(key);
            }
            target = parsed;
            return {};
        };

        auto parseFloat01 = [&](std::optional<float> &target) -> std::optional<std::string> {
            float parsed;
            if (!TryParseFloat01(value, parsed))
            {
                return InvalidParameter(key);
            }
            target = parsed;
            return {};
        };

        auto parseFloat = [&](std::optional<float> &target) -> std::optional<std::string> {
            float parsed;
            if (!TryParseFloat(value, parsed))
            {
                return InvalidParameter(key);
            }
            target = parsed;
            return {};
        };

        std::optional<std::string> error;
        if (key == "level")
        {
            error = parseUint32(options.level);
        }
        else if (key == "quality")
        {
            error = parseUint32(options.quality);
            if (!error && !IsValidQuality(*options.quality))
            {
                error = InvalidParameter(key);
            }
        }
        else if (key == "rarity")
        {
            error = parseUint32(options.rarity);
            if (!error && !IsValidRarity(*options.rarity))
            {
                error = InvalidParameter(key);
            }
        }
        else if (key == "name")
        {
            options.customName = std::string{ value };
        }
        else if (key == "paint")
        {
            error = parseUint32(options.paint);
        }
        else if (key == "seed")
        {
            error = parseUint32(options.seed);
        }
        else if (key == "wear")
        {
            error = parseFloat01(options.wear);
        }
        else if (key == "stattrak")
        {
            error = parseUint32(options.statTrak);
        }
        else if (key == "music")
        {
            error = parseUint32(options.music);
        }
        else if (key == "spray_color")
        {
            error = parseUint32(options.sprayColor);
        }
        else if (key == "spray_remaining")
        {
            error = parseUint32(options.sprayRemaining);
        }
        else if (key == "tournament_event")
        {
            error = parseUint32(options.tournament.eventId);
        }
        else if (key == "tournament_stage")
        {
            error = parseUint32(options.tournament.stageId);
        }
        else if (key == "tournament_team0")
        {
            error = parseUint32(options.tournament.team0Id);
        }
        else if (key == "tournament_team1")
        {
            error = parseUint32(options.tournament.team1Id);
        }
        else if (key == "tournament_mvp")
        {
            error = parseUint32(options.tournament.mvpAccountId);
        }
        else if (key.rfind("sticker", 0) == 0 && key.size() >= 8 && key[7] >= '0' && key[7] <= '5')
        {
            size_t stickerSlot = static_cast<size_t>(key[7] - '0');
            std::string_view suffix{ key.data() + 8, key.size() - 8 };
            if (suffix.empty())
            {
                error = parseUint32(options.sticker[stickerSlot]);
            }
            else if (suffix == "_wear")
            {
                error = parseFloat01(options.stickerWear[stickerSlot]);
            }
            else if (suffix == "_scale")
            {
                error = parseFloat(options.stickerScale[stickerSlot]);
            }
            else if (suffix == "_rotation")
            {
                error = parseFloat(options.stickerRotation[stickerSlot]);
            }
            else
            {
                return UnknownParameter(key);
            }
        }
        else
        {
            return UnknownParameter(key);
        }

        if (error)
        {
            return *error;
        }
    }

    std::vector<CMsgSOSingleObject> updates;
    std::vector<uint64_t> itemIds;
    updates.reserve(count);
    itemIds.reserve(count);

    for (uint32_t i = 0; i < count; i++)
    {
        uint64_t itemId = 0;
        if (hasParameters)
        {
            CMsgSOSingleObject &update = updates.emplace_back();
            std::string error;
            itemId = m_inventory.CreateParameterizedItem(defIndex, options, update, error);
            if (!itemId)
            {
                updates.pop_back();
                if (error == "unknown defindex")
                {
                    return "ERR unknown defindex";
                }
                return std::string{ "ERR " }.append(error);
            }
        }
        else
        {
            itemId = m_inventory.PurchaseItem(defIndex, updates);
        }

        if (!itemId)
        {
            return "ERR unknown defindex";
        }

        itemIds.push_back(itemId);
    }

    for (CMsgSOSingleObject &update : updates)
    {
        SendMessageToGame(true, k_ESOMsg_Create, update);
    }

    std::ostringstream response;
    response << "OK item_ids=";
    for (size_t i = 0; i < itemIds.size(); i++)
    {
        if (i)
        {
            response << ",";
        }
        response << itemIds[i];
    }

    return response.str();
}

std::string ClientGC::RconRemoveItem(const RconRequest &request)
{
    if (request.args.size() != 1)
    {
        return Usage("remove_item <itemid>");
    }

    uint64_t itemId;
    if (!TryParseNumber(request.args[0], itemId))
    {
        return Usage("remove_item <itemid>");
    }

    CMsgSOSingleObject destroyed;
    if (!m_inventory.RemoveItem(itemId, destroyed))
    {
        return "ERR item not found";
    }

    SendMessageToGame(true, k_ESOMsg_Destroy, destroyed);
    return "OK removed";
}

std::string ClientGC::RconRefreshInventory(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("refresh_inventory");
    }

    CMsgSOCacheSubscribed message;
    m_inventory.BuildCacheSubscription(message, GetConfig().Level(), false);
    SendMessageToGame(false, k_ESOMsg_CacheSubscribed, message);
    return "OK refreshed";
}

std::string ClientGC::RconSaveInventory(const RconRequest &request)
{
    if (!request.args.empty())
    {
        return Usage("save_inventory");
    }

    m_inventory.Save();
    return "OK saved";
}

void ClientGC::HandleMessage(uint32_t type, const void *data, uint32_t size)
{
    GCMessageRead messageRead{ type, data, size };
    if (!messageRead.IsValid())
    {
        Platform::Print("ClientGC::HandleMessage: invalid message\n");
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCClientHello:
            OnClientHello(messageRead);
            break;

        case k_EMsgGCAdjustItemEquippedState:
            AdjustItemEquippedState(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientPlayerDecalSign:
            ClientPlayerDecalSign(messageRead);
            break;

        case k_EMsgGCUseItemRequest:
            UseItemRequest(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestJoinServerData:
            ClientRequestJoinServerData(messageRead);
            break;

        case k_EMsgGCSetItemPositions:
            SetItemPositions(messageRead);
            break;

        case k_EMsgGCApplySticker:
            ApplySticker(messageRead);
            break;

        case k_EMsgGCStoreGetUserData:
            StoreGetUserData(messageRead);
            break;

        case k_EMsgGCStorePurchaseInit:
            StorePurchaseInit(messageRead);
            break;

        case k_EMsgGCStorePurchaseFinalize:
            StorePurchaseFinalize(messageRead);
            break;

        case k_EMsgGCCasketItemLoadContents:
            ProcessStorageInspect(messageRead);
            break;

        case k_EMsgGCCasketItemAdd:
            ProcessStorageDeposit(messageRead);
            break;

        case k_EMsgGCCasketItemExtract:
            ProcessStorageWithdraw(messageRead);
            break;

        case k_EMsgGCStatTrakSwap:
            HandleCounterSwapRequest(messageRead);
            break;

        case k_EMsgGCCStrike15_v2_ClientRequestSouvenir:
            HandleRequestSouvenir(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled protobuf message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
    else
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGCDelete:
            DeleteItem(messageRead);
            break;

        case k_EMsgGCUnlockCrate:
            UnlockCrate(messageRead);
            break;

        case k_EMsgGCCraft:
            Craft(messageRead);
            break;

        case k_EMsgGCNameItem:
            NameItem(messageRead);
            break;

        case k_EMsgGCNameBaseItem:
            NameBaseItem(messageRead);
            break;

        case k_EMsgGCRemoveItemName:
            RemoveItemName(messageRead);
            break;

        default:
            Platform::Print("ClientGC::HandleMessage: unhandled struct message %s\n",
                MessageName(messageRead.TypeUnmasked()));
            break;
        }
    }
}

void ClientGC::HandleNetMessage(const void *data, uint32_t size)
{
    // pass 0 as type so it gets parsed from the message
    GCMessageRead messageRead{ 0, data, size };
    if (!messageRead.IsValid())
    {
        Platform::Print("ClientGC::HandleNetMessage: invalid message\n");
        return;
    }

    if (messageRead.IsProtobuf())
    {
        switch (messageRead.TypeUnmasked())
        {
        case k_EMsgGC_IncrementKillCountAttribute:
            IncrementKillCountAttribute(messageRead);
            return;
        }
    }

    Platform::Print("ClientGC::HandleNetMessage: unhandled protobuf message %s\n",
        MessageName(messageRead.TypeUnmasked()));
}

void ClientGC::HandleSOCacheRequest()
{
    CMsgSOCacheSubscribed message;
    m_inventory.BuildCacheSubscription(message, GetConfig().Level(), true);

    GCMessageWrite messageWrite{ k_ESOMsg_CacheSubscribed, message };
    PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
}

void ClientGC::SendMessageToGame(bool sendToGameServer, uint32_t type,
    const google::protobuf::MessageLite &message, uint64_t jobId)
{
    GCMessageWrite messageWrite{ type, message, jobId };

    if (sendToGameServer)
    {
        PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
    }

    PostToHost(HostEvent::Message, messageWrite.TypeMasked(), messageWrite.Data(), messageWrite.Size());
}

constexpr uint32_t MakeAddress(uint32_t v1, uint32_t v2, uint32_t v3, uint32_t v4)
{
    return v4 | (v3 << 8) | (v2 << 16) | (v1 << 24);
}

static void BuildCSWelcome(CMsgCStrike15Welcome &message)
{
    message.set_store_item_hash(136617352);
    message.set_timeplayedconsecutively(0);
    message.set_time_first_played(1329845773);
    message.set_last_time_played(1680260376);
    message.set_last_ip_address(MakeAddress(127, 0, 0, 1));
}

void ClientGC::BuildMatchmakingHello(CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &message)
{
    message.set_account_id(AccountId());

    // this is the state of csgo matchmaking in 2024
    message.mutable_global_stats()->set_players_online(0);
    message.mutable_global_stats()->set_servers_online(0);
    message.mutable_global_stats()->set_players_searching(0);
    message.mutable_global_stats()->set_servers_available(0);
    message.mutable_global_stats()->set_ongoing_matches(0);
    message.mutable_global_stats()->set_search_time_avg(0);

    // don't write search_statistics

    message.mutable_global_stats()->set_main_post_url("");

    // Do not set required_appid_version fields: archived clients interpret them as a minimum build
    // and show "Game update required" when the advertised version is newer than their own.
    message.mutable_global_stats()->set_pricesheet_version(1680057676);
    message.mutable_global_stats()->set_twitch_streams_version(2);
    message.mutable_global_stats()->set_active_tournament_eventid(20);
    message.mutable_global_stats()->set_active_survey_id(0);

    message.set_vac_banned(GetConfig().AccountStatus());
    message.mutable_commendation()->set_cmd_friendly(GetConfig().CommendedFriendly());
    message.mutable_commendation()->set_cmd_teaching(GetConfig().CommendedTeaching());
    message.mutable_commendation()->set_cmd_leader(GetConfig().CommendedLeader());
    message.set_player_level(GetConfig().Level());
    message.set_player_cur_xp(GetConfig().Xp());
}

void ClientGC::BuildClientWelcome(CMsgClientWelcome &message, const CMsgCStrike15Welcome &csWelcome,
    const CMsgGCCStrike15_v2_MatchmakingGC2ClientHello &matchmakingHello)
{
    message.set_version(0); // this is accurate
    message.set_game_data(csWelcome.SerializeAsString());
    m_inventory.BuildCacheSubscription(*message.add_outofdate_subscribed_caches(), GetConfig().Level(), false);
    message.mutable_location()->set_latitude(65.0133006f);
    message.mutable_location()->set_longitude(25.4646212f);
    message.mutable_location()->set_country("FI"); // finland
    message.set_game_data2(matchmakingHello.SerializeAsString());
    message.set_rtime32_gc_welcome_timestamp(static_cast<uint32_t>(time(nullptr)));
    message.set_currency(2); // euros
    message.set_txn_country_code("FI"); // finland
}

void ClientGC::SendRankUpdate()
{
    CMsgGCCStrike15_v2_ClientGCRankUpdate message;

    PlayerRankingInfo *rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().CompetitiveRank());
    rank->set_wins(GetConfig().CompetitiveWins());
    rank->set_rank_type_id(RankTypeCompetitive);

    rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().WingmanRank());
    rank->set_wins(GetConfig().WingmanWins());
    rank->set_rank_type_id(RankTypeWingman);

    rank = message.add_rankings();
    rank->set_account_id(AccountId());
    rank->set_rank_id(GetConfig().DangerZoneRank());
    rank->set_wins(GetConfig().DangerZoneWins());
    rank->set_rank_type_id(RankTypeDangerZone);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientGCRankUpdate, message);
}

void ClientGC::OnClientHello(GCMessageRead &messageRead)
{
    CMsgClientHello hello;
    if (!messageRead.ReadProtobuf(hello))
    {
        Platform::Print("Parsing CMsgClientHello failed, ignoring\n");
        return;
    }

    // we don't care about anything in this message, just reply
    CMsgCStrike15Welcome csWelcome;
    BuildCSWelcome(csWelcome);

    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello mmHello;
    BuildMatchmakingHello(mmHello);

    CMsgClientWelcome clientWelcome;
    BuildClientWelcome(clientWelcome, csWelcome, mmHello);

    SendMessageToGame(false, k_EMsgGCClientWelcome, clientWelcome);

    // the real gc sends this a bit later when it has more info to put on it
    // however we have everything at our fingertips so send it right away
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, mmHello);

    // send all ranks here as well, it's a bit back and forth with real gc
    SendRankUpdate();
}

void ClientGC::AdjustItemEquippedState(GCMessageRead &messageRead)
{
    CMsgAdjustItemEquippedState message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgAdjustItemEquippedState failed, ignoring\n");
        return;
    }

    CMsgSOMultipleObjects update;
    if (!m_inventory.EquipItem(message.item_id(), message.new_class(), message.new_slot(), update))
    {
        // no change
        assert(false);
        return;
    }

    RefreshCachedMusicKitMVPs();

    // let the gameserver know, too
    SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
}

void ClientGC::ClientPlayerDecalSign(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientPlayerDecalSign message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientPlayerDecalSign failed, ignoring\n");
        return;
    }

    if (!Graffiti::SignMessage(*message.mutable_data()))
    {
        Platform::Print("Could not sign graffiti! it won't appear\n");
        return;
    }

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientPlayerDecalSign, message);
}

void ClientGC::UseItemRequest(GCMessageRead &messageRead)
{
    CMsgUseItem message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgUseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroy;
    CMsgSOMultipleObjects updateMultiple;
    CMsgGCItemCustomizationNotification notification;

    if (m_inventory.UseItem(message.item_id(), destroy, updateMultiple, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, updateMultiple);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
}

static void AddressString(uint32_t ip, uint32_t port, char *buffer, size_t bufferSize)
{
    snprintf(buffer, bufferSize,
        "%u.%u.%u.%u:%u\n",
        (ip >> 24) & 0xff,
        (ip >> 16) & 0xff,
        (ip >> 8) & 0xff,
        ip & 0xff,
        port);
}

void ClientGC::ClientRequestJoinServerData(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientRequestJoinServerData request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestJoinServerData failed, ignoring\n");
        return;
    }

    CMsgGCCStrike15_v2_ClientRequestJoinServerData response = request;
    response.mutable_res()->set_serverid(request.version());
    response.mutable_res()->set_direct_udp_ip(request.server_ip());
    response.mutable_res()->set_direct_udp_port(request.server_port());
    response.mutable_res()->set_reservationid(GameServerCookieId);

    char addressString[32];
    AddressString(request.server_ip(), request.server_port(), addressString, sizeof(addressString));
    response.mutable_res()->set_server_address(addressString);

    SendMessageToGame(false, k_EMsgGCCStrike15_v2_ClientRequestJoinServerData, response);
}

void ClientGC::SetItemPositions(GCMessageRead &messageRead)
{
    CMsgSetItemPositions message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgSetItemPositions failed, ignoring\n");
        return;
    }

    std::vector<CMsgItemAcknowledged> acknowledgements;
    acknowledgements.reserve(message.item_positions_size());

    CMsgSOMultipleObjects update;
    if (m_inventory.SetItemPositions(message, acknowledgements, update))
    {
        for (const CMsgItemAcknowledged &acknowledgement : acknowledgements)
        {
            // send these to the server only
            GCMessageWrite messageWrite{ k_EMsgGCItemAcknowledged, acknowledgement };
            PostToHost(HostEvent::NetMessage, 0, messageWrite.Data(), messageWrite.Size());
        }

        SendMessageToGame(true, k_ESOMsg_UpdateMultiple, update);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::IncrementKillCountAttribute(GCMessageRead &messageRead)
{
    CMsgIncrementKillCountAttribute message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgIncrementKillCountAttribute failed, ignoring\n");
        return;
    }

    assert(message.event_type() == 0 || message.event_type() == 1);

    CMsgSOSingleObject update;
    if (m_inventory.IncrementKillCountAttribute(message.item_id(), message.amount(), update))
    {
        RefreshCachedMusicKitMVPs();
        SendMessageToGame(true, k_ESOMsg_Update, update);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::LocalPlayerRoundMVP()
{
    // Music kit StatTrak progress is not driven by the retired official GC path,
    // so mirror the increment locally when the client observes a local round_mvp.
    uint64_t itemId = m_inventory.EquippedMusicKitItemId(true);
    if (!itemId)
    {
        Platform::Print("LocalPlayerRoundMVP: local MVP without equipped StatTrak music kit\n");
        return;
    }

    CMsgSOSingleObject update;
    if (!m_inventory.IncrementKillCountAttribute(itemId, 1, update))
    {
        Platform::Print("LocalPlayerRoundMVP: failed to increment music kit %llu\n", itemId);
        return;
    }

    RefreshCachedMusicKitMVPs();
    Platform::Print("LocalPlayerRoundMVP: incremented music kit %llu\n", itemId);
    SendMessageToGame(true, k_ESOMsg_Update, update);
}

void ClientGC::ApplySticker(GCMessageRead &messageRead)
{
    CMsgApplySticker message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgApplySticker failed, ignoring\n");
        return;
    }

    assert(!message.item_item_id() != !message.baseitem_defidx());

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;

    if (!message.sticker_item_id())
    {
        // scrape
        if (m_inventory.ScrapeSticker(message, update, destroy, notification))
        {
            if (destroy.has_type_id())
            {
                // destroying a default item
                SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
            }

            if (update.has_type_id())
            {
                // if the item got removed (handled above), nothing gets updated
                SendMessageToGame(true, k_ESOMsg_Update, update);
            }

            if (notification.has_request())
            {
                // might get a k_EGCItemCustomizationNotification_RemoveSticker
                SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
            }
        }
        else
        {
            assert(false);
        }
    }
    else if (m_inventory.ApplySticker(message, update, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        SendMessageToGame(true, k_ESOMsg_Update, update);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::StoreGetUserData(GCMessageRead &messageRead)
{
    CMsgStoreGetUserData message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgStoreGetUserData failed, ignoring\n");
        return;
    }

    KeyValue priceSheet{ "price_sheet" };
    if (!priceSheet.ParseFromFile("csgo_gc/price_sheet.txt"))
    {
        return;
    }

    std::string binaryString;
    binaryString.reserve(1 << 17);
    priceSheet.BinaryWriteToString(binaryString);

    // fuck you idiot
    CMsgStoreGetUserDataResponse response;
    response.set_result(1);
    response.set_price_sheet_version(1729); // what
    *response.mutable_price_sheet() = std::move(binaryString);

    SendMessageToGame(false, k_EMsgGCStoreGetUserDataResponse, response);
}

void ClientGC::StorePurchaseInit(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseInit message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseInit failed, ignoring\n");
        return;
    }

    // value doesn't matter
    uint64_t transactionId = Random{}.Integer<uint64_t>();

    assert(!m_transactionId);
    m_transactionId = transactionId;
    m_transactionItemIds.clear();
    m_transactionItemIds.reserve(message.line_items_size()); // rough approx

    // inventory update response
    std::vector<CMsgSOSingleObject> inventoryUpdate;

    for (const auto &item : message.line_items())
    {
        for (uint32_t i = 0; i < item.quantity(); i++)
        {
            uint64_t itemId = m_inventory.PurchaseItem(item.item_def_id(), inventoryUpdate);
            if (!itemId)
            {
                assert(false);
            }
            else
            {
                m_transactionItemIds.push_back(itemId);
            }
        }
    }

    char url[128]; // url doesn't matter, but it needs to be set
    snprintf(url, sizeof(url), "https://checkout.steampowered.com/checkout/approvetxn/%llu/?returnurl=steam", transactionId);

    CMsgGCStorePurchaseInitResponse response;
    response.set_result(1); // success
    response.set_txn_id(transactionId);
    response.set_url(url);
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());

    SendMessageToGame(false, k_EMsgGCStorePurchaseInitResponse, response, messageRead.JobId());

    // server needs to know about new items for validation
    for (auto &newItem : inventoryUpdate)
    {
        SendMessageToGame(true, k_ESOMsg_Create, newItem);
    }

    // this will run the steam callback
    PostToHost(HostEvent::MicroTransactionResponse, 0, nullptr, 0);
}

void ClientGC::StorePurchaseFinalize(GCMessageRead &messageRead)
{
    CMsgGCStorePurchaseFinalize message;
    if (!messageRead.ReadProtobuf(message))
    {
        Platform::Print("Parsing CMsgGCStorePurchaseFinalize failed, ignoring\n");
        return;
    }

    assert(m_transactionId);

    CMsgGCStorePurchaseFinalizeResponse response;
    response.set_result(1); // success
    response.mutable_item_ids()->Assign(m_transactionItemIds.begin(), m_transactionItemIds.end());
    SendMessageToGame(false, k_EMsgGCStorePurchaseFinalizeResponse, response, messageRead.JobId());

    // done with this one
    m_transactionId = 0;
    m_transactionItemIds.clear();
}

void ClientGC::DeleteItem(GCMessageRead &messageRead)
{
    // there is data after this, but i don't know what it is
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCDelete failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject destroyed;
    if (m_inventory.RemoveItem(itemId, destroyed))
    {
        // server needs to know about item destruction for validation
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyed);
    }
    else
    {
        Platform::Print("ClientGC::DeleteItem: item %llu not found\n", itemId);
    }
}

void ClientGC::UnlockCrate(GCMessageRead &messageRead)
{
    uint64_t keyId = messageRead.ReadUint64();
    uint64_t crateId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCUnlockCrate failed, ignoring\n");
        return;
    }

    Platform::Print("CASE OPENING %llu with %llu\n", crateId, keyId);

    CMsgSOSingleObject destroyCrate, destroyKey, newItem;
    CMsgGCItemCustomizationNotification notification;

    const CSOEconItem *crate = m_inventory.GetItem(crateId);
    if (keyId == 0
        && crate
        && crate->def_index() >= SouvenirDefIndexMin
        && crate->def_index() <= SouvenirDefIndexMax
        && m_inventory.OpenSouvenirPackage(crateId, destroyCrate, newItem, notification))
    {
        Platform::Print("SOUVENIR PACKAGE OPENING %llu\n", crateId);

        // OpenSouvenirPackage sets GenerateSouvenir, but the client initiated this
        // via UnlockCrate with keyId=0, so Panorama expects an UnlockCrate completion.
        // Returning GenerateSouvenir here causes a "could not retrieve items" error.
        notification.set_request(k_EGCItemCustomizationNotification_UnlockCrate);

        if (destroyCrate.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroyCrate);
        }

        SendMessageToGame(true, k_ESOMsg_Create, newItem);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
        return;
    }

    if (m_inventory.UnlockCrate(
            crateId,
            keyId,
            destroyCrate,
            destroyKey,
            newItem,
            notification))
    {
        if (destroyCrate.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroyCrate);
        }

        if (destroyKey.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroyKey);
        }

        SendMessageToGame(true, k_ESOMsg_Create, newItem);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::NameItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint64_t itemId = messageRead.ReadUint64();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.NameItem(nameTagId, itemId, name, update, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Update, update);
        if (destroy.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        }

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::NameBaseItem(GCMessageRead &messageRead)
{
    uint64_t nameTagId = messageRead.ReadUint64();
    uint32_t defIndex = messageRead.ReadUint32();
    messageRead.ReadData(1); // skip the sentinel
    std::string_view name = messageRead.ReadString();

    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCNameBaseItem failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject create, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.NameBaseItem(nameTagId, defIndex, name, create, destroy, notification))
    {
        SendMessageToGame(true, k_ESOMsg_Create, create);
        SendMessageToGame(true, k_ESOMsg_Destroy, destroy);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::Craft(GCMessageRead &messageRead)
{
    // Trade-up contract message format:
    // int16_t recipe/filter
    // int16_t itemCount (should be 10)
    // uint64_t itemIds[itemCount]
    
    int16_t recipe = static_cast<int16_t>(messageRead.ReadUint16());
    int16_t itemCount = static_cast<int16_t>(messageRead.ReadUint16());
    
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCCraft header failed, ignoring\n");
        return;
    }
    
    Platform::Print("TRADE-UP CONTRACT: recipe=%d, itemCount=%d\n", recipe, itemCount);

    // Trade-up recipes in items_game.txt use filter -3. The concrete always-known
    // recipe ids are 0..5 for normal weapons and 10..15 for StatTrak weapons.
    bool isTradeUpRecipe = recipe == -3
        || (recipe >= 0 && recipe <= 5)
        || (recipe >= 10 && recipe <= 15);
    if (!isTradeUpRecipe)
    {
        Platform::Print("Unsupported craft recipe %d, ignoring\n", recipe);
        return;
    }
    
    // Read all item IDs
    std::vector<uint64_t> inputItemIds;
    inputItemIds.reserve(itemCount);

    for (int i = 0; i < itemCount; i++)
    {
        uint64_t itemId = messageRead.ReadUint64();
        if (!messageRead.IsValid())
        {
            Platform::Print("Parsing CMsgGCCraft item %d failed, ignoring\n", i);
            return;

        }
        inputItemIds.push_back(itemId);
    }

    Platform::Print("Input items:\n");
    for (uint64_t itemId : inputItemIds)
    {
        const CSOEconItem* item = m_inventory.GetItem(itemId);
        if (item)
        {
            uint32_t paintKitDefIndex = 0;
            uint32_t paintedRarity = item->rarity();
            if (GetItemPaintKitDefIndex(*item, m_inventory.GetItemSchema(), paintKitDefIndex))
            {
                paintedRarity = m_inventory.GetItemSchema().GetPaintedRarity(
                    item->def_index(),
                    paintKitDefIndex,
                    item->rarity());
            }

            std::string collectionId = GetItemCollectionId(*item, m_inventory.GetItemSchema());
            Platform::Print("  Item %llu: def_index %u, paint_kit %u, stored rarity %u, painted rarity %u, quality %u, collection %s (%s)\n",
                itemId, item->def_index(), paintKitDefIndex, item->rarity(), paintedRarity, item->quality(), collectionId.c_str(),
                GetCollectionName(m_inventory.GetItemSchema(), collectionId).c_str());
        }
        else
        {
            Platform::Print("  Item %llu: not found in inventory\n", itemId);
        }
    }

    std::vector<CMsgSOSingleObject> destroyItems;
    CMsgSOSingleObject newItem;
    CMsgGCItemCustomizationNotification notification;
    CSOEconItem *craftedItem = nullptr;
    
    if (m_inventory.TradeUp(inputItemIds, destroyItems, newItem, notification, &craftedItem))
    {
        // Destroy all input items
        for (auto &destroy : destroyItems)
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        }
        
        // Create the new item
        SendMessageToGame(true, k_ESOMsg_Create, newItem);
        
        // Send notification
        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);

        if (craftedItem)
        {
            const ItemInfo *itemInfo = m_inventory.GetItemSchema().ItemInfoByDefIndex(craftedItem->def_index());
            std::string itemName = itemInfo ? itemInfo->m_name : "Unknown Item";

            uint32_t accountId = m_steamId & 0xffffffff;
            std::string chatMessage = "Player " + std::to_string(accountId);
            chatMessage += " has fulfilled a contract and received: ";
            chatMessage += itemName;

            CMsgGCCStrike15_v2_GC2ClientTextMsg textMsg;
            textMsg.set_id(0);
            textMsg.set_type(0);
            textMsg.set_payload(chatMessage);

            SendMessageToGame(true, k_EMsgGCCStrike15_v2_GC2ClientTextMsg, textMsg);
        }
        
        Platform::Print("Trade-up completed successfully!\n");
    }
    else
    {
        Platform::Print("Trade-up failed: input validation failed\n");
    }
}

void ClientGC::RemoveItemName(GCMessageRead &messageRead)
{
    uint64_t itemId = messageRead.ReadUint64();
    if (!messageRead.IsValid())
    {
        Platform::Print("Parsing CMsgGCRemoveItemName failed, ignoring\n");
        return;
    }

    CMsgSOSingleObject update, destroy;
    CMsgGCItemCustomizationNotification notification;
    if (m_inventory.RemoveItemName(itemId, update, destroy, notification))
    {
        if (update.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Update, update);
        }

        if (destroy.has_type_id())
        {
            SendMessageToGame(true, k_ESOMsg_Destroy, destroy);
        }

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
    else
    {
        assert(false);
    }
}

void ClientGC::DispatchStorageResult(const Inventory::StorageTransaction &tx)
{
    using SR = Inventory::StorageResult;
    
    switch (tx.outcome)
    {
    case SR::Success:
    case SR::CapacityExceeded:
        {
            CMsgGCItemCustomizationNotification notice;
            notice.set_request(tx.notificationType);
            notice.add_item_id(tx.affectedContainerId);
            
            if (tx.Succeeded())
            {
                SendMessageToGame(true, k_ESOMsg_Update, tx.itemData);
                SendMessageToGame(true, k_ESOMsg_Update, tx.containerData);
            }
            SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notice);
        }
        break;
        
    case SR::ContainerNotFound:
    case SR::ItemNotFound:
    case SR::InvalidContainerType:
    case SR::InternalError:
        break;
    }
}

void ClientGC::ProcessStorageInspect(GCMessageRead &messageRead)
{
    CMsgCasketItem msg;
    if (!messageRead.ReadProtobuf(msg))
        return;

    CMsgGCItemCustomizationNotification notice;
    notice.set_request(k_EGCItemCustomizationNotification_CasketContents);
    notice.add_item_id(msg.casket_item_id());
    SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notice);
}

void ClientGC::ProcessStorageDeposit(GCMessageRead &messageRead)
{
    CMsgCasketItem msg;
    if (!messageRead.ReadProtobuf(msg))
        return;
    
    auto tx = m_inventory.DepositItemToStorage(msg.casket_item_id(), msg.item_item_id());
    DispatchStorageResult(tx);
}

void ClientGC::ProcessStorageWithdraw(GCMessageRead &messageRead)
{
    CMsgCasketItem msg;
    if (!messageRead.ReadProtobuf(msg))
        return;
    
    auto tx = m_inventory.WithdrawItemFromStorage(msg.casket_item_id(), msg.item_item_id());
    DispatchStorageResult(tx);
}

void ClientGC::BroadcastSwapOutcome(const Inventory::CounterSwapResult &outcome)
{
    using Status = Inventory::CounterSwapStatus;
    
    if (outcome.status != Status::Completed)
        return;
    
    if (outcome.toolRemoval.has_type_id())
        SendMessageToGame(true, k_ESOMsg_Destroy, outcome.toolRemoval);
    
    SendMessageToGame(true, k_ESOMsg_Update, outcome.weaponAUpdate);
    SendMessageToGame(true, k_ESOMsg_Update, outcome.weaponBUpdate);
    
    CMsgGCItemCustomizationNotification notification;
    notification.set_request(k_EGCItemCustomizationNotification_StatTrakSwap);
    notification.add_item_id(outcome.weaponAId);
    notification.add_item_id(outcome.weaponBId);
    SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
}

void ClientGC::HandleCounterSwapRequest(GCMessageRead &messageRead)
{
    CMsgApplyStatTrakSwap request;
    if (!messageRead.ReadProtobuf(request))
        return;

    auto outcome = m_inventory.PerformCounterSwap(
        request.tool_item_id(),
        request.item_1_item_id(),
        request.item_2_item_id()
    );
    
    BroadcastSwapOutcome(outcome);
}

void ClientGC::HandleRequestSouvenir(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_ClientRequestSouvenir request;
    if (!messageRead.ReadProtobuf(request))
    {
        Platform::Print("Parsing CMsgGCCStrike15_v2_ClientRequestSouvenir failed, ignoring\n");
        return;
    }

    Platform::Print("SOUVENIR OPENING %llu\n", request.itemid());

    CMsgSOSingleObject destroyPackage, newItem;
    CMsgGCItemCustomizationNotification notification;

    if (m_inventory.OpenSouvenirPackage(
            request.itemid(),
            destroyPackage,
            newItem,
            notification))
    {
        SendMessageToGame(true, k_ESOMsg_Destroy, destroyPackage);
        SendMessageToGame(true, k_ESOMsg_Create, newItem);

        SendMessageToGame(false, k_EMsgGCItemCustomizationNotification, notification);
    }
}
