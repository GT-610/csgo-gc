// Exercise the native adapter's command queue without loading or calling the engine.
#include "gcmm_native.cpp"

namespace Platform
{
void Print(const char *, ...) {}
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
    auto status = command("gcmm_status");
    valid &= has(status, "\"active\":true") && has(status, "\"all_accepted\":false");
    s_lastSnapshot.roster[1].second = 2;
    valid &= has(command("gcmm_status"), "\"all_accepted\":true");
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
    std::printf("GCMMNativeCommandQueue: %s\n", valid ? "PASS" : "FAIL");
    return valid ? 0 : 1;
}
