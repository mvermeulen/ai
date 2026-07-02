#include "catch.hpp"
#include "output/AsciiPrinter.h"

#include <iostream>
#include <sstream>

namespace {

class CoutCapture {
public:
    CoutCapture() : oldBuf_(std::cout.rdbuf(stream_.rdbuf())) {}

    ~CoutCapture() {
        std::cout.rdbuf(oldBuf_);
    }

    std::string str() const {
        return stream_.str();
    }

private:
    std::ostringstream stream_;
    std::streambuf* oldBuf_;
};

MatchSimulationResults makeResults(const std::vector<std::string>& teams) {
    MatchSimulationResults r;
    r.totalIterations = 1000;

    for (const auto& team : teams) {
        r.group1stProbability[team] = 0.0;
        r.group2ndProbability[team] = 0.0;
        r.group3rdProbability[team] = 0.0;
        r.group4thProbability[team] = 0.0;
        r.r32Probability[team] = 0.0;
        r.r16Probability[team] = 0.0;
        r.qfProbability[team] = 0.0;
        r.sfProbability[team] = 0.0;
        r.finalProbability[team] = 0.0;
        r.championProbability[team] = 0.0;
        r.expectedPoints[team] = 0.0;
    }

    return r;
}

} // namespace

TEST_CASE("Simulation output omits teams eliminated in completed knockout matches", "[AsciiPrinter]") {
    auto results = makeResults({"A", "B", "C", "D"});

    // Group stage resolved, all 4 made R32.
    results.r32Probability["A"] = 1.0;
    results.r32Probability["B"] = 1.0;
    results.r32Probability["C"] = 1.0;
    results.r32Probability["D"] = 1.0;

    // Round of 32 partially resolved: B already eliminated.
    results.r16Probability["A"] = 1.0;
    results.r16Probability["B"] = 0.0;
    results.r16Probability["C"] = 0.6;
    results.r16Probability["D"] = 0.4;

    results.qfProbability["A"] = 0.7;
    results.qfProbability["B"] = 0.0;
    results.qfProbability["C"] = 0.2;
    results.qfProbability["D"] = 0.1;

    results.sfProbability["A"] = 0.45;
    results.sfProbability["B"] = 0.0;
    results.sfProbability["C"] = 0.35;
    results.sfProbability["D"] = 0.20;

    results.finalProbability["A"] = 0.30;
    results.finalProbability["B"] = 0.0;
    results.finalProbability["C"] = 0.40;
    results.finalProbability["D"] = 0.30;

    results.championProbability["A"] = 0.25;
    results.championProbability["B"] = 0.0;
    results.championProbability["C"] = 0.45;
    results.championProbability["D"] = 0.30;

    CoutCapture capture;
    AsciiPrinter::printSimulationResults(results);
    const std::string out = capture.str();

    REQUIRE(out.find("│ B       ") == std::string::npos);
    REQUIRE(out.find("│ A       ") != std::string::npos);
    REQUIRE(out.find("│ C       ") != std::string::npos);
    REQUIRE(out.find("│ D       ") != std::string::npos);
}

TEST_CASE("Simulation output hides completed elimination round columns", "[AsciiPrinter]") {
    auto results = makeResults({"A", "B", "C", "D"});

    // Group and Round of 32 are complete.
    results.r32Probability["A"] = 1.0;
    results.r32Probability["B"] = 1.0;
    results.r32Probability["C"] = 1.0;
    results.r32Probability["D"] = 1.0;

    results.r16Probability["A"] = 1.0;
    results.r16Probability["B"] = 1.0;
    results.r16Probability["C"] = 0.0;
    results.r16Probability["D"] = 0.0;

    // Next round still in progress.
    results.qfProbability["A"] = 0.7;
    results.qfProbability["B"] = 0.3;
    results.qfProbability["C"] = 0.0;
    results.qfProbability["D"] = 0.0;

    results.sfProbability["A"] = 0.5;
    results.sfProbability["B"] = 0.5;
    results.sfProbability["C"] = 0.0;
    results.sfProbability["D"] = 0.0;

    results.finalProbability["A"] = 0.5;
    results.finalProbability["B"] = 0.5;
    results.finalProbability["C"] = 0.0;
    results.finalProbability["D"] = 0.0;

    results.championProbability["A"] = 0.5;
    results.championProbability["B"] = 0.5;
    results.championProbability["C"] = 0.0;
    results.championProbability["D"] = 0.0;

    CoutCapture capture;
    AsciiPrinter::printSimulationResults(results);
    const std::string out = capture.str();

    REQUIRE(out.find("ReachR16") == std::string::npos);
    REQUIRE(out.find("ReachQF") != std::string::npos);
}
