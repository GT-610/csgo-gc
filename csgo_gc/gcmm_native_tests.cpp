// Exercise the native adapter's command queue without loading or calling the engine.
#include "gcmm_native.cpp"
#include <cstdarg>

namespace Platform
{
static std::vector<std::string> s_testLogs;
void Print(const char *format, ...)
{
    char buffer[8192];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    s_testLogs.emplace_back(buffer);
}
}

static bool StatusLoggingIgnoresSampleAge()
{
    using namespace GCMM;
    using Clock = std::chrono::steady_clock;
    const auto savedTaken = s_snapshotTaken;
    const bool savedValid = s_snapshotTakenValid;
    const auto epoch = Clock::time_point{} + std::chrono::seconds{ 10 };
    s_lastPublished.clear();
    const size_t before = Platform::s_testLogs.size();
    SnapshotData snapshot;
    snapshot.readable = true;
    auto publish = [&](Clock::time_point now)
    {
        PublishStatusIfChanged(BoundedResponse(StatusJson(1001, true, true, "", snapshot, std::nullopt, false, now)));
    };
    for (int i = 0; i < 1000; ++i)
    {
        const auto now = epoch + std::chrono::milliseconds{ i * 100 };
        s_snapshotTaken = now - std::chrono::milliseconds{ i % 7 };
        s_snapshotTakenValid = true;
        publish(now);
    }
    bool valid = Platform::s_testLogs.size() == before + 1
        && Platform::s_testLogs.back().find("sample_age_ms") == std::string::npos;
    s_snapshotTaken = epoch;
    auto diagnostic2 = StatusJson(1001, true, true, "", snapshot, std::nullopt, true,
        epoch + std::chrono::milliseconds{ 2 });
    auto diagnostic3 = StatusJson(1001, true, true, "", snapshot, std::nullopt, true,
        epoch + std::chrono::milliseconds{ 3 });
    valid &= diagnostic2.find("\"sample_age_ms\":2") != std::string::npos
        && diagnostic3.find("\"sample_age_ms\":3") != std::string::npos
        && diagnostic2 != diagnostic3;

    // Real state changes must remain observable without periodic log spam.
    snapshot.cookie = 1;
    snapshot.matchId = 2;
    snapshot.expectedHumans = 1;
    snapshot.expiry = 20;
    snapshot.netTime = 1;
    snapshot.roster = {{3, 1}};
    publish(epoch);
    publish(epoch);
    valid &= Platform::s_testLogs.size() == before + 2;
    snapshot.roster[0].second = 2;
    publish(epoch);
    valid &= Platform::s_testLogs.size() == before + 3;
    snapshot.netTime = 2; // Time advancing alone does not change the active state.
    publish(epoch);
    valid &= Platform::s_testLogs.size() == before + 3;
    snapshot.netTime = 21;
    publish(epoch);
    valid &= Platform::s_testLogs.size() == before + 4;
    publish(epoch + std::chrono::seconds{ 3 });
    valid &= Platform::s_testLogs.size() == before + 5
        && Platform::s_testLogs.back().find("\"stale\":true") != std::string::npos;

    s_snapshotTaken = savedTaken;
    s_snapshotTakenValid = savedValid;
    s_lastPublished.clear();
    return valid;
}

int main()
{
    using namespace GCMM;
    auto command = [](std::string_view text) { return ExecuteCommand(text).value_or("unhandled"); };
    auto has = [](const std::string &text, std::string_view part) { return text.find(part) != std::string::npos; };
    bool valid = !ExecuteCommand("status")
        && command("gcmm_status extra=1").starts_with("ERR invalid_argument");
#if defined(_WIN32) && defined(_M_IX86)
    valid &= has(command("gcmm_status"), "\"initialized\":false");
    s_initialized = true;
    s_serverSteamId = 1001ull;
    s_lastSnapshot.readable = true;
    auto first = command("gcmm_reserve reservationid=1 match_id=2 accounts=3,4");
    valid &= has(first, "\"request_id\":\"1\"") && has(first, "\"queued\":true")
        && s_pending && s_pending->id == 1;
    valid &= command("gcmm_reserve reservationid=1 match_id=2 accounts=3,4").starts_with("ERR busy");
    const Reservation owner = s_pending->reservation;
    s_pending.reset(); s_inflight = true;
    valid &= command("gcmm_reserve reservationid=5 match_id=6 accounts=3,4").starts_with("ERR busy")
        && command("gcmm_release reservationid=1 match_id=2").starts_with("ERR busy");
    s_inflight = false;
    s_owned = owner;
    s_lastResult = true;
    s_lastSnapshot.cookie = 1; s_lastSnapshot.matchId = 2;
    s_lastSnapshot.queue = owner.payload;
    s_lastSnapshot.expectedHumans = 2;
    s_lastSnapshot.netTime = 1; s_lastSnapshot.expiry = 20;
    s_lastSnapshot.roster = {{3, 2}, {4, 1}};
    s_snapshotTaken = std::chrono::steady_clock::now();
    s_snapshotTakenValid = true;
    auto status = command("gcmm_status");
    valid &= has(status, "\"active\":true") && has(status, "\"all_accepted\":false")
        && has(status, "\"stale\":false");
    s_lastSnapshot.roster[1].second = 2;
    valid &= has(command("gcmm_status"), "\"all_accepted\":true");

    // A frozen sample must not keep advertising a live reservation.
    s_snapshotTaken = std::chrono::steady_clock::now() - std::chrono::seconds{30};
    auto stale = command("gcmm_status");
    valid &= has(stale, "\"stale\":true") && has(stale, "\"active\":false")
        && has(stale, "\"all_accepted\":false") && !has(stale, "\"active\":true");
    s_snapshotTaken = std::chrono::steady_clock::now();
    valid &= !has(command("gcmm_reserve reservationid=1 match_id=2 accounts=3,4"), "\"queued\":true")
        && !s_pending && s_nextRequest == 1;
    valid &= command("gcmm_reserve reservationid=5 match_id=6 accounts=3,4").starts_with("ERR conflict")
        && command("gcmm_reserve reservationid=1 match_id=2 accounts=3,5").starts_with("ERR conflict")
        && command("gcmm_release reservationid=1 match_id=9").starts_with("ERR stale_owner");
    auto release = command("gcmm_release reservationid=1 match_id=2");
    valid &= has(release, "\"request_id\":\"2\"") && s_pending
        && s_pending->operation == Operation::Release && s_pending->reservation == owner;
    s_pending.reset(); s_owned.reset(); s_completedOwner = owner;
    s_lastSnapshot = {}; s_lastSnapshot.readable = true; s_lastResult = true;
    valid &= has(command("gcmm_release reservationid=1 match_id=2"), "\"last_result\":true")
        && !s_pending && s_nextRequest == 2;
    s_lastSnapshot.cookie = owner.cookie; s_lastSnapshot.matchId = owner.matchId;
    s_lastSnapshot.queue = owner.payload;
    valid &= has(command("gcmm_release reservationid=1 match_id=2"), "\"last_result\":true")
        && !s_pending && s_nextRequest == 2;
    s_lastSnapshot.cookie = 99;
    valid &= command("gcmm_release reservationid=1 match_id=2").starts_with("ERR stale_owner");
    s_lastSnapshot.queue = std::string(2047, '\x01');
    valid &= command("gcmm_status").starts_with("ERR response_too_large");
#else
    valid &= has(command("gcmm_status"), "\"initialized\":false")
        && command("gcmm_reserve reservationid=1 match_id=2 accounts=3").starts_with("ERR unsupported");
#endif
    const bool loggingValid = StatusLoggingIgnoresSampleAge();
    std::printf("GCMMNativeStatusLogging: %s\n", loggingValid ? "PASS" : "FAIL");
    std::printf("GCMMNativeCommandQueue: %s\n", valid ? "PASS" : "FAIL");
    return valid && loggingValid ? 0 : 1;
}
