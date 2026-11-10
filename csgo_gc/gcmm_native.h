#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace GCMM
{
// Called only from the server's Steam callback pass, on the engine thread.
void OnServerFrame(uint64_t serverSteamId);

// RCON threads only queue native operations or read the last engine-thread snapshot.
std::optional<std::string> ExecuteCommand(std::string_view command);
} // namespace GCMM
