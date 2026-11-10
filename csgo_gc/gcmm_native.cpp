#include "stdafx.h"
#include "gcmm_native.h"
#include "gcmm_reservation.h"
#include "config.h"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>

#if defined(_WIN32) && defined(_M_IX86)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mbedtls/md.h>
#endif

namespace GCMM
{
namespace
{
std::mutex s_mutex;

enum class Operation
{
    Reserve,
    Release,
};

struct Request
{
    uint64_t id{};
    Operation operation{};
    Reservation reservation;
};

struct SnapshotData
{
    bool readable{};
    uint64_t cookie{};
    uint64_t matchId{};
    uint32_t expectedHumans{};
    float expiry{};
    double netTime{};
    std::string queue;
    std::vector<std::pair<uint32_t, uint32_t>> roster;
};

std::optional<Request> s_pending;
bool s_inflight{};
std::optional<Reservation> s_owned;
std::optional<Reservation> s_completedOwner;
uint64_t s_nextRequest{};
uint64_t s_requestId{};
uint64_t s_serverSteamId{};
bool s_initialized{};
bool s_lastResult{};
std::string s_discoveryError;
std::string s_lastError;
std::string s_status;
SnapshotData s_lastSnapshot;
bool s_haveSnapshot{};
std::string s_lastPublished;
auto s_nextSample = std::chrono::steady_clock::time_point{};

#if defined(_WIN32) && defined(_M_IX86)
// Exact supported installed 1575 engine. These data-layout RVAs are never used
// unless its on-disk SHA-256 and the public interface's actual slot match.
constexpr char EngineSha256[] = "bcca98731eab8b72d1ef34fa5fe420f26ac98e8dd1d91698171d0ecd9935faa6";
constexpr uintptr_t ServerRva = 0x7c1b00;
constexpr uintptr_t QueueConvarParentRva = 0x5a614c;
constexpr uintptr_t MatchIdRva = 0x7c1e68;
constexpr uintptr_t NetTimeRva = 0x391da98;
constexpr uintptr_t ReserveWrapperRva = 0x1c1c00;
using Factory = void *(__cdecl *)(const char *, int *);
using ReserveFn = bool (__thiscall *)(void *, const char *);

struct EngineApi
{
    bool attempted{};
    uintptr_t base{};
    void *engineServer{};
    ReserveFn reserve{};
    std::string error;
};
EngineApi s_api;

bool Readable(const void *pointer, size_t size)
{
    uintptr_t cursor = reinterpret_cast<uintptr_t>(pointer);
    if (cursor < 0x10000 || size > std::numeric_limits<uintptr_t>::max() - cursor) return false;
    const uintptr_t end = cursor + size;
    while (cursor < end)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void *>(cursor), &info, sizeof(info))
            || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD protection = info.Protect & 0xff;
        if (protection != PAGE_READONLY && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY
            && protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE
            && protection != PAGE_EXECUTE_WRITECOPY) return false;
        const uintptr_t next = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}

template<class T> bool Read(uintptr_t address, T &value)
{
    const void *pointer = reinterpret_cast<const void *>(address);
    if (!Readable(pointer, sizeof(value))) return false;
    memcpy(&value, pointer, sizeof(value));
    return true;
}

bool ReadText(uintptr_t address, std::string &text)
{
    text.clear();
    for (size_t i = 0; i < 2048; ++i)
    {
        char ch{};
        if (!Read(address + i, ch)) return false;
        if (!ch) return true;
        text += ch;
    }
    return false;
}

std::string FileHash(HMODULE module)
{
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return {};
    std::ifstream file{std::filesystem::path{path}, std::ios::binary};
    if (!file) return {};
    std::vector<unsigned char> data{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    if (file.bad() || data.empty()) return {};
    unsigned char hash[32]{};
    if (mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data.data(), data.size(), hash)) return {};
    std::ostringstream output;
    for (unsigned char ch : hash) output << std::hex << std::setfill('0') << std::setw(2) << unsigned(ch);
    return output.str();
}

bool Initialize()
{
    if (s_api.attempted) return s_api.reserve != nullptr;
    HMODULE module = GetModuleHandleW(L"engine.dll");
    if (!module) return false;
    s_api.attempted = true;
    if (FileHash(module) != EngineSha256)
    {
        s_api.error = "unsupported_engine";
        return false;
    }
    s_api.base = reinterpret_cast<uintptr_t>(module);
    auto factory = reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface"));
    int returnCode = 1;
    s_api.engineServer = factory ? factory("VEngineServer023", &returnCode) : nullptr;
    uintptr_t table{}, target{};
    if (!s_api.engineServer || returnCode != 0
        || !Read(reinterpret_cast<uintptr_t>(s_api.engineServer), table)
        || !Read(table + 149 * sizeof(uintptr_t), target)
        || target != s_api.base + ReserveWrapperRva)
    {
        s_api.error = "unsupported_interface";
        return false;
    }
    static const unsigned char prefix[]{0x55,0x8b,0xec,0x53,0x56,0x8b,0x75,0x08,0x57,0x56,0x8b,0xf9};
    if (!Readable(reinterpret_cast<void *>(target), sizeof(prefix))
        || memcmp(reinterpret_cast<void *>(target), prefix, sizeof(prefix)))
    {
        s_api.error = "unsupported_wrapper";
        return false;
    }
    s_api.reserve = reinterpret_cast<ReserveFn>(target);
    Platform::Print("GCMM: enabled verified 1575 VEngineServer023 slot 149 on server callback thread\n");
    return true;
}
#endif

std::string JsonEscape(std::string_view text)
{
    std::string result;
    result.reserve(text.size() + 2);
    for (unsigned char ch : text)
    {
        switch (ch)
        {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                std::ostringstream escaped;
                escaped << "\\u" << std::hex << std::setfill('0') << std::setw(4) << unsigned(ch);
                result += escaped.str();
            }
            else result += static_cast<char>(ch);
            break;
        }
    }
    return result;
}

std::string Hex(uint64_t value)
{
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

std::string StatusJson(uint64_t serverSteamId, bool enabled, bool initialized,
    const std::string &error, const SnapshotData &snapshot, const std::optional<Reservation> &owned)
{
    uint32_t minimumStage = snapshot.roster.empty() ? 0 : std::numeric_limits<uint32_t>::max();
    uint32_t accepted = 0;
    bool matches = snapshot.readable && owned && snapshot.cookie == owned->cookie && snapshot.matchId == owned->matchId
        && snapshot.queue == owned->payload && snapshot.roster.size() == owned->accounts.size()
        && snapshot.expectedHumans == owned->accounts.size();
    std::vector<uint32_t> seen;
    for (const auto &[account, stage] : snapshot.roster)
    {
        minimumStage = (std::min)(minimumStage, stage);
        if (stage >= 2) ++accepted;
        if (std::find(seen.begin(), seen.end(), account) != seen.end()) matches = false;
        seen.push_back(account);
        if (owned && std::find(owned->accounts.begin(), owned->accounts.end(), account) == owned->accounts.end())
            matches = false;
    }
    const bool active = snapshot.readable && snapshot.cookie != 0 && std::isfinite(snapshot.expiry)
        && std::isfinite(snapshot.netTime) && snapshot.expiry > snapshot.netTime;
    std::ostringstream output;
    output << "OK {\"protocol\":\"gcmm/1\",\"role\":\"server\",\"enabled\":"
        << (enabled ? "true" : "false") << ",\"initialized\":" << (initialized ? "true" : "false")
        << ",\"request_id\":\"" << s_requestId << "\",\"last_result\":"
        << (s_lastResult ? "true" : "false") << ",\"server_steamid\":\"" << serverSteamId
        << "\",\"cookie\":\"" << Hex(snapshot.cookie) << "\",\"match_id\":\""
        << Hex(snapshot.matchId) << "\",\"queue\":\"" << JsonEscape(snapshot.queue)
        << "\",\"roster_count\":" << snapshot.roster.size() << ",\"expected_humans\":"
        << snapshot.expectedHumans << ",\"minimum_stage\":" << minimumStage << ",\"accepted\":" << accepted
        << ",\"matches_request\":" << (matches ? "true" : "false") << ",\"active\":"
        << (active ? "true" : "false") << ",\"all_accepted\":"
        << (matches && active && !snapshot.roster.empty() && minimumStage >= 2 ? "true" : "false")
        << ",\"roster\":[";
    for (size_t index = 0; index < snapshot.roster.size(); ++index)
    {
        if (index) output << ',';
        output << "{\"account_id\":" << snapshot.roster[index].first
            << ",\"stage\":" << snapshot.roster[index].second << '}';
    }
    output << ']';
    if (!error.empty()) output << ",\"error\":\"" << JsonEscape(error) << '"';
    output << '}';
    return output.str();
}

#if defined(_WIN32) && defined(_M_IX86)
SnapshotData ReadSnapshot()
{
    SnapshotData snapshot;
    const uintptr_t server = s_api.base + ServerRva;
    uint32_t array{}, count{}, convar{}, textPointer{};
    if (!Read(server + 0x2f0, snapshot.cookie) || !Read(s_api.base + MatchIdRva, snapshot.matchId)
        || !Read(server + 0x2dc, array) || !Read(server + 0x2e8, count)
        || !Read(server + 0x304, snapshot.expectedHumans) || !Read(server + 0x2fc, snapshot.expiry)
        || !Read(s_api.base + NetTimeRva, snapshot.netTime)
        || !Read(s_api.base + QueueConvarParentRva, convar)
        || !Read(uintptr_t(convar) + 0x24, textPointer) || !ReadText(textPointer, snapshot.queue)
        || count > 64)
        return snapshot;
    snapshot.roster.reserve(count);
    for (uint32_t index = 0; index < count; ++index)
    {
        uint32_t account{}, stage{};
        const uintptr_t player = uintptr_t(array) + index * 0x2c;
        if (!Read(player, account) || !Read(player + 0x28, stage))
        {
            snapshot.roster.clear();
            return snapshot;
        }
        snapshot.roster.emplace_back(account, stage);
    }
    snapshot.readable = true;
    return snapshot;
}
#endif

std::optional<std::pair<std::string, std::string>> CommandName(std::string_view command)
{
    std::istringstream input{std::string{command}};
    std::string name;
    input >> name;
    for (char &ch : name) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    std::string remainder;
    std::getline(input, remainder);
    if (!remainder.empty() && remainder.front() == ' ') remainder.erase(0, 1);
    if (name != "gcmm_status" && name != "gcmm_reserve" && name != "gcmm_release") return std::nullopt;
    return std::make_pair(std::move(name), std::move(remainder));
}

std::string DisabledStatus()
{
    SnapshotData empty;
    return StatusJson(0, false, false, "disabled", empty, std::nullopt);
}

std::string UnsupportedStatus()
{
    SnapshotData empty;
#if defined(_WIN32) && defined(_M_IX86)
    const std::string error = s_api.error.empty() ? "engine_unavailable" : s_api.error;
#else
    const std::string error = "unsupported_platform";
#endif
    return StatusJson(0, true, false, error, empty, std::nullopt);
}

std::vector<std::string> SplitArguments(std::string_view text)
{
    std::istringstream input{std::string{text}};
    std::vector<std::string> args;
    for (std::string arg; input >> arg;) args.push_back(std::move(arg));
    return args;
}

std::string OperationError(std::string_view code, std::string_view message)
{
    return "ERR " + std::string{code} + ' ' + std::string{message};
}

std::string BoundedResponse(std::string response)
{
    constexpr size_t MaxBody = 4096 - 10;
    if (response.size() <= MaxBody) return response;
    return "ERR response_too_large GCMM JSON response exceeds Source RCON limit";
}

std::string QueuedAck(uint64_t requestId)
{
    std::ostringstream output;
    output << "OK {\"protocol\":\"gcmm/1\",\"role\":\"server\",\"enabled\":true"
        << ",\"initialized\":true,\"request_id\":\"" << requestId
        << "\",\"queued\":true}";
    return output.str();
}

bool SnapshotActive(const SnapshotData &snapshot)
{
    return snapshot.readable && snapshot.cookie != 0 && std::isfinite(snapshot.expiry)
        && std::isfinite(snapshot.netTime) && snapshot.expiry > snapshot.netTime;
}

bool SnapshotOwns(const SnapshotData &snapshot, const Reservation &reservation)
{
    return snapshot.readable && snapshot.cookie == reservation.cookie
        && snapshot.matchId == reservation.matchId && snapshot.queue == reservation.payload;
}

std::string StatusLocked()
{
    return BoundedResponse(StatusJson(s_serverSteamId, GetConfig().m_gcmmEnabled, s_initialized,
        s_initialized ? s_lastError : s_discoveryError, s_lastSnapshot, s_owned));
}

} // namespace

std::optional<std::string> ExecuteCommand(std::string_view command)
{
    const auto parsedCommand = CommandName(command);
    if (!parsedCommand) return std::nullopt;
    const auto &[name, remainder] = *parsedCommand;
    if (name == "gcmm_status" && !remainder.empty())
        return OperationError("invalid_argument", "gcmm_status takes no arguments");

    if (!GetConfig().m_gcmmEnabled)
    {
        if (name == "gcmm_status")
        {
            std::lock_guard lock{s_mutex};
            return StatusLocked();
        }
        return OperationError("disabled", "GCMM is disabled");
    }
#if !defined(_WIN32) || !defined(_M_IX86)
    if (name == "gcmm_status") return UnsupportedStatus();
    return OperationError("unsupported", "native server adapter supports verified Windows x86 build 1575 only");
#else
    bool initialized{};
    std::string discoveryError;
    {
        std::lock_guard lock{s_mutex};
        initialized = s_initialized;
        discoveryError = s_discoveryError;
    }
    if (name == "gcmm_status")
    {
        std::lock_guard lock{s_mutex};
        return StatusLocked();
    }
    if (!initialized)
        return OperationError("unsupported", discoveryError.empty() ? "native engine unavailable" : discoveryError);

    const std::vector<std::string> args = SplitArguments(remainder);
    if (name == "gcmm_reserve")
    {
        Reservation reservation;
        if (std::string error = BuildReservation(args, reservation); !error.empty()) return error;
        std::lock_guard lock{s_mutex};
        if (s_pending || s_inflight) return OperationError("busy", "another native request is pending");
        if (SnapshotActive(s_lastSnapshot))
        {
            if (s_owned && SnapshotOwns(s_lastSnapshot, *s_owned) && *s_owned == reservation)
                return StatusLocked();
            return OperationError("conflict", "an active native reservation is owned by another request");
        }
        if (s_nextRequest == std::numeric_limits<uint64_t>::max())
            return OperationError("overflow", "request_id exhausted");
        const uint64_t id = ++s_nextRequest;
        s_requestId = id;
        s_lastResult = false;
        s_lastError.clear();
        s_completedOwner.reset();
        s_pending = Request{id, Operation::Reserve, std::move(reservation)};
        return QueuedAck(id);
    }

    if (args.size() != 2)
        return OperationError("invalid_argument", "gcmm_release requires reservationid and match_id");
    Reservation requested;
    if (std::string error = BuildReservation({args[0], args[1], "accounts=1"}, requested); !error.empty())
        return error;
    std::lock_guard lock{s_mutex};
    if (s_pending || s_inflight) return OperationError("busy", "another native request is pending");
    if (s_owned && s_owned->cookie == requested.cookie && s_owned->matchId == requested.matchId)
    {
        if (!SnapshotActive(s_lastSnapshot) && s_lastSnapshot.readable
            && s_lastSnapshot.cookie == 0 && s_lastSnapshot.queue.empty())
        {
            s_completedOwner = s_owned;
            s_owned.reset();
            s_lastResult = true;
            s_lastError.clear();
            return StatusLocked();
        }
        requested.accounts = s_owned->accounts;
        requested.payload = s_owned->payload;
    }
    else if (s_completedOwner && s_completedOwner->cookie == requested.cookie
        && s_completedOwner->matchId == requested.matchId
        && !SnapshotActive(s_lastSnapshot) && s_lastSnapshot.readable
        && (SnapshotOwns(s_lastSnapshot, *s_completedOwner)
            || (s_lastSnapshot.cookie == 0 && s_lastSnapshot.queue.empty())))
    {
        s_lastResult = true;
        s_lastError.clear();
        return StatusLocked();
    }
    else
    {
        return OperationError("stale_owner", "reservation owner no longer matches");
    }
    if (s_nextRequest == std::numeric_limits<uint64_t>::max())
        return OperationError("overflow", "request_id exhausted");
    const uint64_t id = ++s_nextRequest;
    s_requestId = id;
    s_lastResult = false;
    s_lastError.clear();
    s_pending = Request{id, Operation::Release, std::move(requested)};
    return QueuedAck(id);
#endif
}

void OnServerFrame(uint64_t serverSteamId)
{
    if (!GetConfig().m_gcmmEnabled)
    {
        std::lock_guard lock{s_mutex};
        s_status = DisabledStatus();
        return;
    }
#if defined(_WIN32) && defined(_M_IX86)
    if (!Initialize())
    {
        std::lock_guard lock{s_mutex};
        SnapshotData empty;
        s_initialized = false;
        s_discoveryError = s_api.error.empty() ? "engine_unavailable" : s_api.error;
        s_serverSteamId = serverSteamId;
        s_status = StatusJson(serverSteamId, true, false, s_discoveryError, empty, s_owned);
        return;
    }
    {
        std::lock_guard lock{s_mutex};
        s_initialized = true;
        s_discoveryError.clear();
        s_serverSteamId = serverSteamId;
    }
    std::optional<Request> request;
    std::optional<Reservation> ownedBefore;
    {
        std::lock_guard lock{s_mutex};
        request.swap(s_pending);
        if (request) s_inflight = true;
        ownedBefore = s_owned;
    }
    if (request)
    {
        const SnapshotData before = ReadSnapshot();
        bool result = false;
        std::string error;
        if (!before.readable)
        {
            error = "snapshot_unreadable";
        }
        else if (request->operation == Operation::Reserve)
        {
            const bool active = before.cookie != 0 && std::isfinite(before.expiry)
                && std::isfinite(before.netTime) && before.expiry > before.netTime;
            const bool sameOwner = ownedBefore && before.cookie == ownedBefore->cookie
                && before.matchId == ownedBefore->matchId && before.queue == ownedBefore->payload;
            if (active)
            {
                if (sameOwner && *ownedBefore == request->reservation) result = true;
                else error = "native reservation already active";
            }
            else
            {
                Platform::Print("GCMM: request %llu ReserveServerForQueuedGame(%s)\n",
                    request->id, request->reservation.payload.c_str());
                result = s_api.reserve(s_api.engineServer, request->reservation.payload.c_str());
                Platform::Print("GCMM: request %llu returned %d\n", request->id, result);
            }
        }
        else
        {
            const bool ownedNative = SnapshotOwns(before, request->reservation);
            const bool ownerStillAbsent = before.cookie == 0 && before.queue.empty();
            if (!ownedNative && ownerStillAbsent)
            {
                // A timeout or an earlier successful release is idempotent for
                // the same completed owner, provided no new reservation exists.
                result = true;
            }
            else if (!ownedNative)
            {
                error = "native reservation owner is no longer active";
            }
            else
            {
                std::ostringstream release;
                release << 'Q' << std::hex << request->reservation.cookie << ',' << request->reservation.matchId << ",0:";
                Platform::Print("GCMM: request %llu releasing %s\n", request->id, release.str().c_str());
                result = s_api.reserve(s_api.engineServer, release.str().c_str());
                Platform::Print("GCMM: request %llu release returned %d\n", request->id, result);
            }
        }
        {
            std::lock_guard lock{s_mutex};
            s_lastResult = result;
            s_lastError = std::move(error);
            if (request->operation == Operation::Reserve && result)
            {
                s_owned = request->reservation;
                s_completedOwner.reset();
            }
            if (request->operation == Operation::Release && result)
            {
                s_completedOwner = request->reservation;
                s_owned.reset();
            }
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (now < s_nextSample && !request) return;
    s_nextSample = now + std::chrono::milliseconds{100};
    const SnapshotData snapshot = ReadSnapshot();
    std::string status;
    {
        std::lock_guard lock{s_mutex};
        s_serverSteamId = serverSteamId;
        s_lastSnapshot = snapshot;
        s_haveSnapshot = true;
        if (request) s_inflight = false;
        status = BoundedResponse(StatusJson(serverSteamId, true, true, s_lastError, snapshot, s_owned));
        s_status = status;
    }
    if (status != s_lastPublished)
    {
        Platform::Print("GCMM: %s\n", status.c_str());
        s_lastPublished = status;
    }
#else
    (void)serverSteamId;
    std::lock_guard lock{s_mutex};
    s_status = UnsupportedStatus();
#endif
}
} // namespace GCMM
