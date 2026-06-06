#include "catch.hpp"
#include "model/Match.h"

TEST_CASE("Match model statuses and outcome checks", "[Match]") {
    Match match(1, "group", "A", "2026-06-11", "MEX", "RSA", -1, -1, -1, -1, "scheduled");

    REQUIRE(match.matchId() == 1);
    REQUIRE(match.stage() == "group");
    REQUIRE(match.group() == "A");
    REQUIRE(match.date() == "2026-06-11");
    REQUIRE(match.homeTeam() == "MEX");
    REQUIRE(match.awayTeam() == "RSA");
    REQUIRE(match.isScheduled() == true);
    REQUIRE(match.isPlayed() == false);

    // Score update
    match.setScore(2, 1, -1, -1, "final");
    REQUIRE(match.isScheduled() == false);
    REQUIRE(match.isFinal() == true);
    REQUIRE(match.isPlayed() == true);
    REQUIRE(match.homeScore() == 2);
    REQUIRE(match.awayScore() == 1);
    REQUIRE(match.homeTeamWon() == true);
    REQUIRE(match.awayTeamWon() == false);
    REQUIRE(match.isTie() == false);

    // Tie check
    Match tieMatch(2, "group", "A", "2026-06-12", "KOR", "CZE", 1, 1, -1, -1, "final");
    REQUIRE(tieMatch.isTie() == true);
    REQUIRE(tieMatch.homeTeamWon() == false);
    REQUIRE(tieMatch.awayTeamWon() == false);

    // Penalty check (knockout)
    Match koMatch(73, "knockout_r32", "N/A", "2026-06-28", "ARG", "FRA", 2, 2, 5, 4, "final");
    REQUIRE(koMatch.isTie() == true); // score tie
    REQUIRE(koMatch.homeTeamWon() == true); // penalty winner
    REQUIRE(koMatch.awayTeamWon() == false);
}
