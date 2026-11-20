#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace GCMM
{

struct Reservation
{
    uint64_t cookie{};
    uint64_t matchId{};
    std::vector<uint32_t> accounts;
    std::string payload;

    bool operator==(const Reservation &other) const
    {
        return cookie == other.cookie && matchId == other.matchId && accounts == other.accounts
            && payload == other.payload;
    }
};

// Strictly bounded Q-reservation fixture. It performs no engine calls.
std::string BuildReservation(const std::vector<std::string> &args, Reservation &reservation);

} // namespace GCMM
