#include "gcmm_reservation.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <sstream>
#include <string_view>

namespace GCMM
{
namespace
{
bool ParseUnsigned(std::string_view text, uint64_t &value)
{
    int base = 10;
    if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
    {
        text.remove_prefix(2);
        base = 16;
    }
    if (text.empty()) return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}
}

std::string BuildReservation(const std::vector<std::string> &args, Reservation &reservation)
{
    Reservation parsed;
    bool sawCookie = false, sawMatch = false, sawAccounts = false;
    for (const std::string &arg : args)
    {
        const size_t equals = arg.find('=');
        if (equals == std::string::npos || !equals || equals + 1 == arg.size())
            return "ERR invalid_argument expected nonempty key=value";
        const std::string_view key{arg.data(), equals};
        std::string_view value{arg.data() + equals + 1, arg.size() - equals - 1};
        if (key == "reservationid")
        {
            if (sawCookie) return "ERR invalid_argument duplicate reservationid";
            sawCookie = true;
            if (!ParseUnsigned(value, parsed.cookie) || !parsed.cookie)
                return "ERR invalid_argument invalid reservationid";
        }
        else if (key == "match_id")
        {
            if (sawMatch) return "ERR invalid_argument duplicate match_id";
            sawMatch = true;
            if (!ParseUnsigned(value, parsed.matchId) || !parsed.matchId)
                return "ERR invalid_argument invalid match_id";
        }
        else if (key == "accounts")
        {
            if (sawAccounts) return "ERR invalid_argument duplicate accounts";
            sawAccounts = true;
            if (value.empty()) return "ERR invalid_argument empty accounts";
            while (true)
            {
                const size_t comma = value.find(',');
                const std::string_view account = value.substr(0, comma);
                uint64_t id{};
                if (!ParseUnsigned(account, id) || !id || id > std::numeric_limits<uint32_t>::max())
                    return "ERR invalid_argument invalid account id";
                const auto accountId = static_cast<uint32_t>(id);
                if (std::find(parsed.accounts.begin(), parsed.accounts.end(), accountId) != parsed.accounts.end())
                    return "ERR invalid_argument duplicate account id";
                parsed.accounts.push_back(accountId);
                if (parsed.accounts.size() > 64) return "ERR invalid_argument too many accounts";
                if (comma == std::string_view::npos) break;
                value.remove_prefix(comma + 1);
                if (value.empty()) return "ERR invalid_argument empty account id";
            }
        }
        else return "ERR invalid_argument unknown key: " + std::string{key};
    }
    if (!sawCookie || !sawMatch || !sawAccounts)
        return "ERR invalid_argument required reservationid, match_id and accounts";
    std::ostringstream payload;
    payload << 'Q' << std::hex << parsed.cookie << ',' << parsed.matchId << ",1:";
    for (uint32_t account : parsed.accounts) payload << '[' << account << ']';
    parsed.payload = payload.str();
    reservation = std::move(parsed);
    return {};
}
} // namespace GCMM
