#pragma once

// Matchmaking protocol probe.
//
// P0 verification tooling for the community matchmaking protocol design
// (docs/spec/mm). It answers "what does the real 1575 client actually send and
// accept" by recording matchmaking-range GC messages at the Steam GC boundary,
// and by building the GC->client messages (9104/9107/9112) a tester injects
// over RCON. Nothing here changes behavior unless "mm_probe" is enabled in
// csgo_gc/config.txt or a tester issues an mm_* RCON command.

namespace MMProbe
{

constexpr uint32_t FirstMatchmakingMessage = 9100;
constexpr uint32_t LastMatchmakingMessage = 9199;

constexpr const char *LogPath = "csgo_gc/mm_probe.log";

bool IsMatchmakingMessage(uint32_t type);

// Called from the Steam GC proxy for every message crossing the boundary.
// server: true for the dedicated server's GC, false for the game client's.
// outbound: true for game->GC (SendMessage), false for GC->game (RetrieveMessage).
// Does nothing unless the probe is enabled and the type is in the MM range.
void Observe(bool server, bool outbound, uint32_t type, const void *data, uint32_t size);

// Render one framed GC message (type + header + body) as text: a summary line,
// a decoded field tree of the protobuf body, and a hex dump. The decoder is
// schema-less on purpose: fields the proto file does not know still show up,
// which is exactly what an unknown-client-behavior investigation needs.
std::string FormatMessage(bool server, bool outbound, uint32_t type, const void *data, uint32_t size);

// Schema-less protobuf field dump. Returns false when the bytes are not a
// well-formed protobuf message; out then holds whatever decoded before the error.
bool DecodeProtobuf(const uint8_t *data, size_t size, int depth, std::string &out);

// Parse "a.b.c.d" into the host-order value CS:GO messages carry
// (a is the most significant byte).
bool ParseIPv4(std::string_view text, uint32_t &ip);

// RCON argument builders. args are key=value tokens; "self" expands to the
// local account id where an account list is accepted. Only fields that are
// named are set, so a tester can subtract fields one at a time (Q-04).
// Returns an empty string on success, otherwise an "ERR ..." message.
std::string BuildReserve(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientReserve &message);
std::string BuildUpdate(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientUpdate &message);
std::string BuildAbandon(const std::vector<std::string> &args, uint32_t selfAccountId,
    CMsgGCCStrike15_v2_MatchmakingGC2ClientAbandon &message);

} // namespace MMProbe
