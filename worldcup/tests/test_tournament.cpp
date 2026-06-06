#include "catch.hpp"
#include "model/Tournament.h"

TEST_CASE("Tournament manages teams, matches, and standings", "[Tournament]") {
    Tournament tour;

    Team team1("USA", "United States", "D", 1675.0, "CONCACAF");
    Team team2("TUR", "Turkey", "D", 1575.0, "UEFA");

    tour.addTeam(team1);
    tour.addTeam(team2);

    REQUIRE(tour.getTeam("USA") != nullptr);
    REQUIRE(tour.getTeam("USA")->fullName() == "United States");
    REQUIRE(tour.getTeam("TUR") != nullptr);
    REQUIRE(tour.getTeam("XYZ") == nullptr);

    Match match(1, "group", "D", "2026-06-12", "USA", "TUR", 2, 1, -1, -1, "final");
    tour.addMatch(match);

    tour.computeStandings();

    REQUIRE(tour.getTeam("USA")->wins() == 1);
    REQUIRE(tour.getTeam("USA")->points() == 3);
    REQUIRE(tour.getTeam("USA")->goalsFor() == 2);
    REQUIRE(tour.getTeam("USA")->goalsAgainst() == 1);

    REQUIRE(tour.getTeam("TUR")->losses() == 1);
    REQUIRE(tour.getTeam("TUR")->points() == 0);
    REQUIRE(tour.getTeam("TUR")->goalsFor() == 1);
    REQUIRE(tour.getTeam("TUR")->goalsAgainst() == 2);
}
