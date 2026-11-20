#include "gcmm_reservation.h"

#include <iostream>
#include <string>
#include <vector>

int main()
{
    using GCMM::BuildReservation;
    using GCMM::Reservation;
    bool valid = true;
    auto check = [&valid](bool condition, const char *name)
    {
        if (!condition) { std::cerr << "FAILED: " << name << '\n'; valid = false; }
    };
    Reservation reservation;
    check(BuildReservation({"reservationid=0x1122334455667788", "match_id=0x0a0b0c0d", "accounts=305419896"}, reservation).empty(), "valid single participant");
    check(reservation.cookie == 0x1122334455667788ull && reservation.matchId == 0xa0b0c0d, "full-width identities");
    check(reservation.payload == "Q1122334455667788,a0b0c0d,1:[12345678]", "native hexadecimal payload");
    check(BuildReservation({"reservationid=18446744073709551615", "match_id=1", "accounts=0x12345678,4294967295"}, reservation).empty(), "unsigned bounds and account list");
    check(reservation.accounts.size() == 2 && reservation.accounts[1] == 0xffffffffu, "account ids preserved");

    const std::vector<std::vector<std::string>> bad{
        {},
        {"reservationid=1", "match_id=2"},
        {"reservationid=0", "match_id=2", "accounts=3"},
        {"reservationid=1", "match_id=0", "accounts=3"},
        {"reservationid=-1", "match_id=2", "accounts=3"},
        {"reservationid=18446744073709551616", "match_id=2", "accounts=3"},
        {"reservationid=1junk", "match_id=2", "accounts=3"},
        {"reservationid=1", "match_id=2", "accounts="},
        {"reservationid=1", "match_id=2", "accounts=0"},
        {"reservationid=1", "match_id=2", "accounts=4294967296"},
        {"reservationid=1", "match_id=2", "accounts=self"},
        {"reservationid=1", "match_id=2", "accounts=3,3"},
        {"reservationid=1", "match_id=2", "accounts=3,"},
        {"reservationid=1", "match_id=2", "accounts=,3"},
        {"reservationid=1", "match_id=2", "accounts=3,,4"},
        {"reservationid=1", "reservationid=2", "match_id=2", "accounts=3"},
        {"reservationid=1", "match_id=2", "match_id=3", "accounts=3"},
        {"reservationid=1", "match_id=2", "accounts=3", "accounts=4"},
        {"reservationid=1", "match_id=2", "accounts=3", "arbitrary=4"},
        {"reservationid=1", "match_id=2", "accounts=3", "broken"},
    };
    const std::string previous = reservation.payload;
    for (const auto &args : bad)
    {
        check(!BuildReservation(args, reservation).empty(), "reject invalid fixture");
        check(reservation.payload == previous, "no partial output on rejection");
    }
    std::string accounts;
    for (unsigned i = 1; i <= 65; ++i) { if (i > 1) accounts += ','; accounts += std::to_string(i); }
    check(!BuildReservation({"reservationid=1", "match_id=2", "accounts=" + accounts}, reservation).empty(), "bounded participant count");
    std::cout << (valid ? "Native reservation input tests passed\n" : "Native reservation input tests failed\n");
    return valid ? 0 : 1;
}
