#pragma once

#include <functional>

// Watch page passthrough (issue #152).
//
// The local ClientGC answers every match list request with an empty
// CMsgGCCStrike15_v2_MatchList and answers tournament prediction requests with an
// empty CMsgGCCStrike15_v2_Predictions. The Watch page therefore renders the baked
// tournament scaffold and then has nothing to show, and Pick'em never leaves its
// loading state.
//
// With "watch_passthrough" enabled in csgo_gc/config.txt the client hello and the
// tournament requests are forwarded to the real Valve GC on the game's own Steam
// pipe, and the replies that carry tournament data are folded back into the local
// message queue. The local GC keeps answering everything else, so inventory, the
// store and matchmaking are unaffected. The real welcome and whatever SOCache
// traffic follows it are dropped on purpose: the local GC already owns that
// conversation, and a second inventory subscription would fight it.
//
// This is a diagnostic mode. It only produces data while the app the game is
// logged on to still has a live CS:GO GC, which is app 4465480 for the standalone
// legacy listing.

namespace WatchPassthrough
{

// Mirrors EGCResults so callers in the Steam GC proxy can cast without pulling in
// the Steam headers here.
constexpr int ResultOK = 0;
constexpr int ResultNoMessage = 1;
constexpr int ResultBufferTooSmall = 2;

using SendFn = std::function<int(uint32_t, const void *, uint32_t)>;
using AvailableFn = std::function<bool(uint32_t *)>;
using RetrieveFn = std::function<int(uint32_t *, void *, uint32_t, uint32_t *)>;

bool IsEnabled();

// Version to advertise in the forwarded client hello, or 0 to forward the
// client's own. The real GC gates its tournament data on the client build, which
// is why the issue report needed a bumped version number.
uint32_t HelloVersionOverride();

// True for the client hello variants. Only these get their version rewritten.
bool IsHello(uint32_t type);

// True for the request types that are forwarded instead of answered locally: the
// tournament match list requests and the tournament prediction request.
bool Forwards(uint32_t type);

// The Steam GC proxy installs its own trampolines here through the coordinator
// entry points it was handed. self identifies the original interface, so a
// recreated interface replaces the stored trampolines instead of leaving stale
// ones behind.
void AttachSend(void *self, const SendFn &send);
void AttachAvailable(void *self, const AvailableFn &available);
void AttachRetrieve(void *self, const RetrieveFn &retrieve);
void DetachAll();

// Forwards one request to the real GC, rewriting the hello version when one is
// configured. Returns false when no trampoline is installed yet, so the caller can
// keep answering locally.
bool Forward(uint32_t type, const void *data, uint32_t size);

// Drains the real GC queue and appends the replies that carry tournament data as
// (masked type, framed bytes) pairs. Types that are not kept are dropped and
// reported once per type through droppedTypes, so the log says what the upstream
// backend actually answered.
void Pump(std::vector<std::pair<uint32_t, std::vector<uint8_t>>> &keep,
    std::vector<uint32_t> &droppedTypes);

} // namespace WatchPassthrough
