#include "catch.hpp"
#include "model/MonteCarlo.h"

TEST_CASE("MonteCarlo simulation engine parameters and matches", "[MonteCarlo]") {
    MonteCarlo mc;
    mc.setModelParameters(1.5, 0.002, 0.2);

    REQUIRE(mc.baseRate() == 1.5);
    REQUIRE(mc.alpha() == 0.002);
    REQUIRE(mc.hostAdvantage() == 0.2);

    Team t1("ARG", "Argentina", "A", 1900.0, "CONMEBOL");
    Team t2("FRA", "France", "A", 1800.0, "UEFA");

    // Host checks
    REQUIRE(MonteCarlo::isHost("USA") == true);
    REQUIRE(MonteCarlo::isHost("MEX") == true);
    REQUIRE(MonteCarlo::isHost("CAN") == true);
    REQUIRE(MonteCarlo::isHost("ARG") == false);

    // Goal expectations: Elo diff = 100, no hosts
    auto expected = mc.getExpectedGoals(t1, t2);
    // lambdaHome = 1.5 * exp(0.002 * 100) = 1.5 * exp(0.2) ~ 1.5 * 1.2214 = 1.832
    // lambdaAway = 1.5 * exp(-0.2) ~ 1.5 * 0.8187 = 1.228
    REQUIRE(expected.first > 1.8);
    REQUIRE(expected.first < 1.9);
    REQUIRE(expected.second > 1.2);
    REQUIRE(expected.second < 1.3);

    // If one is USA (host)
    Team t3("USA", "United States", "D", 1600.0, "CONCACAF");
    auto expectedHost = mc.getExpectedGoals(t3, t2); // USA is home and host
    // lambdaHome = 1.5 * exp(0.002 * -200 + 0.2) = 1.5 * exp(-0.4 + 0.2) = 1.5 * exp(-0.2) ~ 1.228
    REQUIRE(expectedHost.first > 1.2);
    REQUIRE(expectedHost.first < 1.3);
}
