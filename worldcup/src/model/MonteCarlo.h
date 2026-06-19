#ifndef MONTECARLO_H
#define MONTECARLO_H

#include "Tournament.h"
#include <map>
#include <vector>
#include <string>
#include <random>

struct TeamOutcome {
    std::map<std::string, int> group1st;
    std::map<std::string, int> group2nd;
    std::map<std::string, int> group3rd;
    std::map<std::string, int> group4th;
    std::map<std::string, int> makeR32;
    std::map<std::string, int> makeR16;
    std::map<std::string, int> makeQF;
    std::map<std::string, int> makeSF;
    std::map<std::string, int> makeFinal;
    std::map<std::string, int> winChampion;
    std::map<std::string, double> avgGoals;
    std::map<std::string, double> avgPoints;
};

struct MatchSimulationResults {
    int totalIterations;
    std::map<std::string, double> group1stProbability;
    std::map<std::string, double> group2ndProbability;
    std::map<std::string, double> group3rdProbability;
    std::map<std::string, double> group4thProbability;
    std::map<std::string, double> r32Probability;
    std::map<std::string, double> r16Probability;
    std::map<std::string, double> qfProbability;
    std::map<std::string, double> sfProbability;
    std::map<std::string, double> finalProbability;
    std::map<std::string, double> championProbability;
    std::map<std::string, double> expectedPoints;
    TeamOutcome outcomes;
};

struct GameImpact {
    int matchId;
    std::string date;
    std::string homeTeam;
    std::string awayTeam;
    double homeDeltaR32;
    double awayDeltaR32;
    double homeR32IfHomeWins;
    double awayR32IfAwayWins;
};

struct ImpactAnalysisResults {
    std::vector<GameImpact> gameImpacts;
};

class MonteCarlo {
public:
    MonteCarlo() = default;

    MatchSimulationResults simulate(const Tournament& tournament,
                                    int iterations = 100000,
                                    unsigned int seed = 0);

    ImpactAnalysisResults analyzeImpact(const Tournament& tournament,
                                        int iterations = 10000,
                                        unsigned int seed = 12345);

    // Get goal expectations lambda for two teams
    std::pair<double, double> getExpectedGoals(const Team& home, const Team& away) const;

    void setModelParameters(double baseRate, double alpha, double hostAdvantage);
    bool loadModelParameters(const std::string& path);
    void saveModelParameters(const std::string& path) const;

    double baseRate() const { return baseRate_; }
    double alpha() const { return alpha_; }
    double hostAdvantage() const { return hostAdvantage_; }

    static bool isHost(const std::string& abbr) {
        return abbr == "USA" || abbr == "MEX" || abbr == "CAN";
    }

private:
    double baseRate_ = 1.35;       // Average goals per team per match
    double alpha_ = 0.0016;        // Sensitivity to Elo difference
    double hostAdvantage_ = 0.18;  // Additive boost for host goal rates
    mutable std::mt19937 rng_;

    TeamOutcome simulateIteration(const Tournament& tournament, std::mt19937& rng) const;
    void simulateMatch(const Team& home, const Team& away, bool isKnockout,
                       int& homeScore, int& awayScore, int& homePenalty, int& awayPenalty,
                       std::mt19937& rng) const;
};

// Fitting algorithm to calibrate baseRate, alpha, and hostAdvantage from historical past_world_cups.csv
struct FitResult {
    double baseRate;
    double alpha;
    double hostAdvantage;
    double nll;
};

FitResult fitPoissonModel(const std::string& historicalCsvPath);

#endif // MONTECARLO_H
