#include "stdafx.h"
#include "gc_client.h"
#include "gc_message.h"
#include "gcmm_reservation.h"
#include "mm_probe.h"

#include <limits>

namespace
{
using Arguments = std::unordered_map<std::string, std::string>;

bool Number(std::string_view text, uint64_t &value)
{
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty()) return false;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

std::string ArgumentsFor(const std::vector<std::string> &args,
    std::initializer_list<std::string_view> allowed, Arguments &values)
{
    for (const auto &arg : args)
    {
        const size_t separator = arg.find('=');
        if (separator == std::string::npos || !separator || separator + 1 == arg.size())
            return "ERR invalid_argument expected nonempty key=value";
        std::string key = arg.substr(0, separator);
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return "ERR invalid_argument unknown key: " + key;
        if (!values.emplace(std::move(key), arg.substr(separator + 1)).second)
            return "ERR invalid_argument duplicate key";
    }
    return {};
}

std::string SearchOwner(const Arguments &args, uint64_t current, uint64_t &search)
{
    const auto it = args.find("search_id");
    if (it == args.end() || !Number(it->second, search) || !search)
        return "ERR invalid_argument nonzero search_id required";
    if (search != current) return "ERR stale_search search_id no longer current";
    return {};
}

std::string Hex(uint64_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

std::string Ip(uint32_t ip)
{
    return std::to_string(ip >> 24) + '.' + std::to_string((ip >> 16) & 255)
        + '.' + std::to_string((ip >> 8) & 255) + '.' + std::to_string(ip & 255);
}

bool MapName(std::string_view map)
{
    return !map.empty() && map.size() <= 128 && std::all_of(map.begin(), map.end(), [](unsigned char ch)
    {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '/';
    });
}
}

std::string ClientGC::RconGCMMStatus(const RconRequest &request)
{
    if (!request.args.empty()) return "ERR invalid_argument usage: gcmm_status";
    const char *phase = "idle";
    switch (m_gcmmPhase)
    {
    case GCMMPhase::Searching: phase = "searching"; break;
    case GCMMPhase::Reserved: phase = "reserved"; break;
    case GCMMPhase::Committed: phase = "committed"; break;
    case GCMMPhase::Idle: break;
    }
    std::ostringstream json;
    json << "OK {\"protocol\":\"gcmm/1\",\"role\":\"client\",\"enabled\":"
        << (GetConfig().m_gcmmEnabled ? "true" : "false") << ",\"steam_id\":\"" << m_steamId
        << "\",\"account_id\":" << AccountId() << ",\"search_id\":\"" << m_gcmmSearchId
        << "\",\"phase\":\"" << phase << "\",\"request\":{\"account_ids\":[";
    for (int i = 0; i < m_gcmmRequest.account_ids_size(); ++i)
    {
        if (i) json << ',';
        json << m_gcmmRequest.account_ids(i);
    }
    json << "],\"game_type\":" << m_gcmmRequest.game_type()
        << ",\"client_version\":" << m_gcmmRequest.client_version() << "},\"reservation\":";
    if (!m_gcmmReservation) json << "null";
    else
    {
        const auto &reserve = *m_gcmmReservation;
        json << "{\"reservationid\":\"" << Hex(reserve.reservationid())
            << "\",\"match_id\":\"" << Hex(reserve.reservation().match_id()) << "\",\"accounts\":[";
        for (int i = 0; i < reserve.reservation().account_ids_size(); ++i)
        {
            if (i) json << ',';
            json << reserve.reservation().account_ids(i);
        }
        json << "],\"ip\":\"" << Ip(reserve.direct_udp_ip()) << "\",\"port\":" << reserve.direct_udp_port()
            << ",\"map\":\"" << reserve.map() << "\",\"game_type\":" << reserve.reservation().game_type()
            << ",\"serverid\":\"" << reserve.serverid() << "\"}";
    }
    json << '}';
    return json.str();
}

void ClientGC::PublishGCMMHello()
{
    CMsgGCCStrike15_v2_MatchmakingGC2ClientHello hello;
    BuildMatchmakingHello(hello);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientHello, hello);
}

void ClientGC::HandleMatchmakingStart(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStart request;
    if (!messageRead.ReadProtobuf(request) || !GetConfig().m_gcmmEnabled) return;
    std::unordered_set<uint32_t> accounts;
    bool includesLocal = false;
    for (uint32_t account : request.account_ids())
    {
        if (!account || !accounts.insert(account).second) return;
        includesLocal |= account == AccountId();
    }
    if (!includesLocal || accounts.size() > 64 || m_gcmmSearchId == std::numeric_limits<uint64_t>::max()) return;
    const bool hadOngoing = m_gcmmPhase == GCMMPhase::Committed || m_probeOngoingMatch.has_value();
    ++m_gcmmSearchId;
    m_gcmmRequest = std::move(request);
    m_gcmmReservation.reset();
    m_probeOngoingMatch.reset();
    m_gcmmPhase = GCMMPhase::Searching;
    if (hadOngoing) PublishGCMMHello();
    Platform::Print("GCMM: native search %llu for account %u (%d party accounts)\n",
        m_gcmmSearchId, AccountId(), m_gcmmRequest.account_ids_size());
}

void ClientGC::HandleMatchmakingStop(GCMessageRead &messageRead)
{
    CMsgGCCStrike15_v2_MatchmakingStop request;
    if (!messageRead.ReadProtobuf(request) || !GetConfig().m_gcmmEnabled) return;
    // The native connect handoff can stop the search too. Explicit abandon
    // clears the match, while an ordinary stop preserves committed handoff.
    if (request.abandon() != 0)
    {
        const bool hadOngoing = m_gcmmReservation.has_value() || m_probeOngoingMatch.has_value();
        m_gcmmReservation.reset();
        m_probeOngoingMatch.reset();
        m_gcmmPhase = GCMMPhase::Idle;
        if (hadOngoing) PublishGCMMHello();
    }
    else if (m_gcmmPhase != GCMMPhase::Committed)
    {
        m_gcmmReservation.reset();
        m_gcmmPhase = GCMMPhase::Idle;
    }
}

std::string ClientGC::RconGCMMReserve(const RconRequest &request)
{
    if (!GetConfig().m_gcmmEnabled) return "ERR disabled GCMM is disabled";
    Arguments args;
    if (auto error = ArgumentsFor(request.args, {"search_id", "reservationid", "match_id", "accounts", "ip", "port",
        "serverid", "map", "game_type", "server_address", "server_version"}, args); !error.empty()) return error;
    uint64_t search{};
    if (auto error = SearchOwner(args, m_gcmmSearchId, search); !error.empty()) return error;
    for (const char *required : {"reservationid", "match_id", "accounts", "ip", "port", "serverid", "map", "game_type"})
        if (!args.contains(required)) return "ERR invalid_argument missing key: " + std::string{required};
    GCMM::Reservation ids;
    if (auto error = GCMM::BuildReservation({"reservationid=" + args.at("reservationid"),
        "match_id=" + args.at("match_id"), "accounts=" + args.at("accounts")}, ids); !error.empty())
        return "ERR invalid_argument invalid reservation identities or roster";
    if (std::find(ids.accounts.begin(), ids.accounts.end(), AccountId()) == ids.accounts.end())
        return "ERR invalid_argument roster must include local account";
    for (uint32_t partyAccount : m_gcmmRequest.account_ids())
        if (std::find(ids.accounts.begin(), ids.accounts.end(), partyAccount) == ids.accounts.end())
            return "ERR invalid_argument roster must include every party account";
    std::vector<std::string> fields;
    for (const auto &[key, value] : args) if (key != "search_id") fields.push_back(key + '=' + value);
    CMsgGCCStrike15_v2_MatchmakingGC2ClientReserve reserve;
    if (auto error = MMProbe::BuildReserve(fields, AccountId(), reserve); !error.empty())
        return "ERR invalid_argument invalid reservation fields";
    const uint32_t ip = reserve.direct_udp_ip();
    if (!(ip >> 24) || (ip >> 24) >= 224 || args.at("ip") != Ip(ip) || !reserve.direct_udp_port() || !reserve.serverid())
        return "ERR invalid_argument unicast IPv4, nonzero port and serverid required";
    const std::string address = Ip(ip) + ':' + std::to_string(reserve.direct_udp_port());
    if (reserve.has_server_address() && reserve.server_address() != address)
        return "ERR invalid_argument server_address must match ip:port";
    if (!MapName(reserve.map())) return "ERR invalid_argument invalid map name";
    if ((reserve.reservation().game_type() & 15) != 8 || (m_gcmmRequest.game_type() & 15) != 8)
        return "ERR unsupported_mode GCMM v1 native ACCEPT profile requires competitive";
    if (reserve.reservation().has_server_version()
        && reserve.reservation().server_version() != m_gcmmRequest.client_version())
        return "ERR invalid_argument server_version must match native client version";
    reserve.set_server_address(address);
    reserve.mutable_reservation()->set_server_version(m_gcmmRequest.client_version());
    if (m_gcmmReservation)
    {
        if (m_gcmmReservation->SerializeAsString() == reserve.SerializeAsString()) return RconGCMMStatus({});
        return "ERR conflict another reservation already owns this search";
    }
    if (m_gcmmPhase != GCMMPhase::Searching) return "ERR not_searching no active native search";
    m_gcmmReservation = std::move(reserve);
    m_gcmmPhase = GCMMPhase::Reserved;
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate update;
    update.set_matchmaking(1);
    update.add_waiting_account_id_sessions(AccountId());
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate, update);
    update.Clear();
    update.set_matchmaking(4);
    for (uint32_t account : ids.accounts)
    {
        update.add_waiting_account_id_sessions(account);
        update.add_ongoingmatch_account_id_sessions(account);
    }
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate, update);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientReserve, *m_gcmmReservation);
    return RconGCMMStatus({});
}

std::string ClientGC::RconGCMMCommit(const RconRequest &request)
{
    if (!GetConfig().m_gcmmEnabled) return "ERR disabled GCMM is disabled";
    Arguments args;
    if (auto error = ArgumentsFor(request.args, {"search_id", "reservationid", "match_id"}, args); !error.empty()) return error;
    uint64_t search{}, cookie{}, match{};
    if (auto error = SearchOwner(args, m_gcmmSearchId, search); !error.empty()) return error;
    if (!args.contains("reservationid") || !args.contains("match_id")
        || !Number(args.at("reservationid"), cookie) || !cookie || !Number(args.at("match_id"), match) || !match)
        return "ERR invalid_argument nonzero reservationid and match_id required";
    if (!m_gcmmReservation || m_gcmmReservation->reservationid() != cookie
        || m_gcmmReservation->reservation().match_id() != match)
        return "ERR stale_reservation reservation owner no longer current";
    if (m_gcmmPhase == GCMMPhase::Committed) return RconGCMMStatus({});
    if (m_gcmmPhase != GCMMPhase::Reserved) return "ERR not_reserved search is not reserved";
    // Readiness is authoritative at the native server. A trusted coordinator
    // must verify the complete matching roster at stage >= 2 before this call.
    m_gcmmPhase = GCMMPhase::Committed;
    PublishGCMMHello();
    return RconGCMMStatus({});
}

std::string ClientGC::RconGCMMCancel(const RconRequest &request)
{
    if (!GetConfig().m_gcmmEnabled) return "ERR disabled GCMM is disabled";
    Arguments args;
    if (auto error = ArgumentsFor(request.args, {"search_id"}, args); !error.empty()) return error;
    uint64_t search{};
    if (auto error = SearchOwner(args, m_gcmmSearchId, search); !error.empty()) return error;
    if (m_gcmmPhase == GCMMPhase::Idle && !m_gcmmReservation) return RconGCMMStatus({});
    m_gcmmPhase = GCMMPhase::Idle;
    m_gcmmReservation.reset();
    m_probeOngoingMatch.reset();
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate update;
    update.set_matchmaking(0);
    SendMessageToGame(false, k_EMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate, update);
    PublishGCMMHello();
    return RconGCMMStatus({});
}
