#include "catch.hpp"
#include "model/Team.h"

TEST_CASE("Team model properties and record updates", "[Team]") {
    Team team("ARG", "Argentina", "A", 1860.0, "CONMEBOL");

    REQUIRE(team.abbreviation() == "ARG");
    REQUIRE(team.fullName() == "Argentina");
    REQUIRE(team.group() == "A");
    REQUIRE(team.eloRating() == 1860.0);
    REQUIRE(team.federation() == "CONMEBOL");

    // Default record
    REQUIRE(team.wins() == 0);
    REQUIRE(team.draws() == 0);
    REQUIRE(team.losses() == 0);
    REQUIRE(team.points() == 0);
    REQUIRE(team.goalsFor() == 0);
    REQUIRE(team.goalsAgainst() == 0);
    REQUIRE(team.goalDifference() == 0);

    // Add match outcomes
    team.addWin();
    team.addGoalsFor(3);
    team.addGoalsAgainst(1);
    REQUIRE(team.wins() == 1);
    REQUIRE(team.points() == 3);
    REQUIRE(team.goalsFor() == 3);
    REQUIRE(team.goalsAgainst() == 1);
    REQUIRE(team.goalDifference() == 2);

    team.addDraw();
    team.addGoalsFor(1);
    team.addGoalsAgainst(1);
    REQUIRE(team.draws() == 1);
    REQUIRE(team.points() == 4);
    REQUIRE(team.goalsFor() == 4);
    REQUIRE(team.goalsAgainst() == 2);

    // Reset
    team.resetRecord();
    REQUIRE(team.wins() == 0);
    REQUIRE(team.draws() == 0);
    REQUIRE(team.points() == 0);
    REQUIRE(team.goalsFor() == 0);
    REQUIRE(team.goalsAgainst() == 0);
}
