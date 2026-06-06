#include "catch.hpp"
#include "model/Tiebreaker.h"

TEST_CASE("Tiebreaker breaks ties using group and head-to-head criteria", "[Tiebreaker]") {
    std::vector<Team*> groupTeams;
    Team t1("ARG", "Argentina", "A", 1860, "CONMEBOL");
    Team t2("FRA", "France", "A", 1840, "UEFA");
    Team t3("KOR", "South Korea", "A", 1560, "AFC");
    Team t4("RSA", "South Africa", "A", 1410, "CAF");

    groupTeams.push_back(&t1);
    groupTeams.push_back(&t2);
    groupTeams.push_back(&t3);
    groupTeams.push_back(&t4);

    // Give them group stats
    // Team 1: 2 wins, 0 draws, 1 loss = 6 points, GF: 5, GA: 2 (GD: +3)
    t1.addWin(); t1.addWin(); t1.addLoss();
    t1.addGoalsFor(5); t1.addGoalsAgainst(2);

    // Team 2: 2 wins, 0 draws, 1 loss = 6 points, GF: 4, GA: 2 (GD: +2)
    t2.addWin(); t2.addWin(); t2.addLoss();
    t2.addGoalsFor(4); t2.addGoalsAgainst(2);

    // Team 3: 1 win, 0 draws, 2 losses = 3 points, GF: 2, GA: 5 (GD: -3)
    t3.addWin(); t3.addLoss(); t3.addLoss();
    t3.addGoalsFor(2); t3.addGoalsAgainst(5);

    // Team 4: 1 win, 0 draws, 2 losses = 3 points, GF: 2, GA: 4 (GD: -2)
    t4.addWin(); t4.addLoss(); t4.addLoss();
    t4.addGoalsFor(2); t4.addGoalsAgainst(4);

    // Match fixtures
    std::vector<Match> matches = {
        Match(1, "group", "A", "2026-06-11", "ARG", "FRA", 2, 0, -1, -1, "final"),
        Match(2, "group", "A", "2026-06-11", "KOR", "RSA", 1, 0, -1, -1, "final")
    };

    auto sorted = Tiebreaker::breakGroupTie(groupTeams, matches);

    REQUIRE(sorted[0]->abbreviation() == "ARG"); // 6 pts, +3 GD
    REQUIRE(sorted[1]->abbreviation() == "FRA"); // 6 pts, +2 GD
    REQUIRE(sorted[2]->abbreviation() == "RSA"); // 3 pts, -2 GD
    REQUIRE(sorted[3]->abbreviation() == "KOR"); // 3 pts, -3 GD
}

TEST_CASE("Tiebreaker ranks third-place teams across groups", "[Tiebreaker]") {
    Team t1("ARG", "Argentina", "A", 1860, "CONMEBOL");
    Team t2("FRA", "France", "B", 1840, "UEFA");
    Team t3("KOR", "South Korea", "C", 1560, "AFC");

    // Add stats
    // ARG: 4 points, GD +1, GF 3
    t1.addWin(); t1.addDraw();
    t1.addGoalsFor(3); t1.addGoalsAgainst(2);

    // FRA: 4 points, GD +1, GF 2
    t2.addWin(); t2.addDraw();
    t2.addGoalsFor(2); t2.addGoalsAgainst(1);

    // KOR: 3 points, GD 0, GF 2
    t3.addWin(); t3.addLoss();
    t3.addGoalsFor(2); t3.addGoalsAgainst(2);

    std::vector<Team*> thirds = {&t3, &t1, &t2};
    auto ranked = Tiebreaker::rankThirdPlaces(thirds);

    REQUIRE(ranked[0]->abbreviation() == "ARG"); // 4 pts, GD +1, GF 3
    REQUIRE(ranked[1]->abbreviation() == "FRA"); // 4 pts, GD +1, GF 2
    REQUIRE(ranked[2]->abbreviation() == "KOR"); // 3 pts
}
