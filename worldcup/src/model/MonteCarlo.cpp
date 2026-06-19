#include "MonteCarlo.h"
#include "Tiebreaker.h"
#include "util/CsvParser.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace {

double sigmoid(double x) {
    return 1.0 / (1.0 + std::exp(-x));
}

} // namespace

std::pair<double, double> MonteCarlo::getExpectedGoals(const Team& home, const Team& away) const {
    double eloDiff = home.eloRating() - away.eloRating();
    double homeBoost = isHost(home.abbreviation()) ? hostAdvantage_ : 0.0;
    double awayBoost = isHost(away.abbreviation()) ? hostAdvantage_ : 0.0;

    double lambdaHome = baseRate_ * std::exp(alpha_ * eloDiff + homeBoost);
    double lambdaAway = baseRate_ * std::exp(-alpha_ * eloDiff + awayBoost);

    return {lambdaHome, lambdaAway};
}

void MonteCarlo::simulateMatch(const Team& home, const Team& away, bool isKnockout,
                               int& homeScore, int& awayScore, int& homePenalty, int& awayPenalty,
                               std::mt19937& rng) const {
    auto expected = getExpectedGoals(home, away);
    std::poisson_distribution<int> homeDist(expected.first);
    std::poisson_distribution<int> awayDist(expected.second);

    homeScore = homeDist(rng);
    awayScore = awayDist(rng);
    homePenalty = -1;
    awayPenalty = -1;

    if (isKnockout && homeScore == awayScore) {
        // Extra time (30 minutes - lambda scaled by 1/3)
        std::poisson_distribution<int> homeETDist(expected.first / 3.0);
        std::poisson_distribution<int> awayETDist(expected.second / 3.0);

        homeScore += homeETDist(rng);
        awayScore += awayETDist(rng);

        if (homeScore == awayScore) {
            // Penalty shootout: 50-50 coin flip
            std::uniform_real_distribution<> coin(0.0, 1.0);
            if (coin(rng) < 0.5) {
                homePenalty = 5;
                awayPenalty = 4;
            } else {
                homePenalty = 4;
                awayPenalty = 5;
            }
        }
    }
}

TeamOutcome MonteCarlo::simulateIteration(const Tournament& tournament, std::mt19937& rng) const {
    Tournament sim = tournament;
    auto& matches = sim.allMatches();

    // 1. Simulate unplayed group matches
    for (auto& match : matches) {
        if (match.stage() == "group" && !match.isPlayed()) {
            const Team* home = sim.getTeam(match.homeTeam());
            const Team* away = sim.getTeam(match.awayTeam());
            if (home && away) {
                int hs, as, hp, ap;
                simulateMatch(*home, *away, false, hs, as, hp, ap, rng);
                match.setScore(hs, as, hp, ap, "final");
            }
        }
    }

    // 2. Compute group standings
    sim.computeStandings();

    // 3. Allocate Round of 32 pairings
    Tiebreaker::allocateRoundOf32Matchups(sim);

    // 4. Track progression
    TeamOutcome outcome;
    for (const auto& group : sim.getGroups()) {
        auto standings = sim.teamsByGroup(group);
        if (standings.size() > 0) outcome.group1st[standings[0]->abbreviation()]++;
        if (standings.size() > 1) outcome.group2nd[standings[1]->abbreviation()]++;
        if (standings.size() > 2) outcome.group3rd[standings[2]->abbreviation()]++;
        if (standings.size() > 3) outcome.group4th[standings[3]->abbreviation()]++;
    }

    for (const auto& [abbr, team] : sim.allTeams()) {
        outcome.avgGoals[abbr] = team.goalsFor();
        outcome.avgPoints[abbr] = team.points();
    }

    // Map match ID -> index in vector for fast access
    std::map<int, size_t> matchIndexMap;
    for (size_t i = 0; i < matches.size(); ++i) {
        matchIndexMap[matches[i].matchId()] = i;
    }

    auto getMatchRef = [&](int id) -> Match& {
        return matches[matchIndexMap[id]];
    };

    // Helper to simulate and propagate a knockout match
    auto runKnockoutMatch = [&](int matchId, std::string& winnerOut, std::string& loserOut) {
        Match& match = getMatchRef(matchId);
        const Team* home = sim.getTeam(match.homeTeam());
        const Team* away = sim.getTeam(match.awayTeam());
        if (home && away) {
            int hs, as, hp, ap;
            simulateMatch(*home, *away, true, hs, as, hp, ap, rng);
            match.setScore(hs, as, hp, ap, "final");
            if (match.homeTeamWon()) {
                winnerOut = match.homeTeam();
                loserOut = match.awayTeam();
            } else {
                winnerOut = match.awayTeam();
                loserOut = match.homeTeam();
            }
        } else {
            winnerOut = "";
            loserOut = "";
        }
    };

    // --- Round of 32 (Matches 73 to 88) ---
    std::vector<std::string> r32Winners(16);
    std::vector<std::string> r32Losers(16);
    for (int id = 73; id <= 88; ++id) {
        outcome.makeR32[getMatchRef(id).homeTeam()]++;
        outcome.makeR32[getMatchRef(id).awayTeam()]++;

        runKnockoutMatch(id, r32Winners[id - 73], r32Losers[id - 73]);
    }

    // Set R16 matchups (Matches 89 to 96)
    // Winner 73 vs 74
    getMatchRef(89).setTeams(r32Winners[0], r32Winners[1]);
    // Winner 75 vs 76
    getMatchRef(90).setTeams(r32Winners[2], r32Winners[3]);
    // Winner 77 vs 78
    getMatchRef(91).setTeams(r32Winners[4], r32Winners[5]);
    // Winner 79 vs 80
    getMatchRef(92).setTeams(r32Winners[6], r32Winners[7]);
    // Winner 81 vs 82
    getMatchRef(93).setTeams(r32Winners[8], r32Winners[9]);
    // Winner 83 vs 84
    getMatchRef(94).setTeams(r32Winners[10], r32Winners[11]);
    // Winner 85 vs 86
    getMatchRef(95).setTeams(r32Winners[12], r32Winners[13]);
    // Winner 87 vs 88
    getMatchRef(96).setTeams(r32Winners[14], r32Winners[15]);

    // --- Round of 16 (Matches 89 to 96) ---
    std::vector<std::string> r16Winners(8);
    std::vector<std::string> r16Losers(8);
    for (int id = 89; id <= 96; ++id) {
        outcome.makeR16[getMatchRef(id).homeTeam()]++;
        outcome.makeR16[getMatchRef(id).awayTeam()]++;

        runKnockoutMatch(id, r16Winners[id - 89], r16Losers[id - 89]);
    }

    // Set Quarterfinal matchups (Matches 97 to 100)
    // Winner 89 vs 90
    getMatchRef(97).setTeams(r16Winners[0], r16Winners[1]);
    // Winner 91 vs 92
    getMatchRef(98).setTeams(r16Winners[2], r16Winners[3]);
    // Winner 93 vs 94
    getMatchRef(99).setTeams(r16Winners[4], r16Winners[5]);
    // Winner 95 vs 96
    getMatchRef(100).setTeams(r16Winners[6], r16Winners[7]);

    // --- Quarterfinals (Matches 97 to 100) ---
    std::vector<std::string> qfWinners(4);
    std::vector<std::string> qfLosers(4);
    for (int id = 97; id <= 100; ++id) {
        outcome.makeQF[getMatchRef(id).homeTeam()]++;
        outcome.makeQF[getMatchRef(id).awayTeam()]++;

        runKnockoutMatch(id, qfWinners[id - 97], qfLosers[id - 97]);
    }

    // Set Semifinal matchups (Matches 101 to 102)
    // Winner 97 vs 98
    getMatchRef(101).setTeams(qfWinners[0], qfWinners[1]);
    // Winner 99 vs 100
    getMatchRef(102).setTeams(qfWinners[2], qfWinners[3]);

    // --- Semifinals (Matches 101 to 102) ---
    std::vector<std::string> sfWinners(2);
    std::vector<std::string> sfLosers(2);
    for (int id = 101; id <= 102; ++id) {
        outcome.makeSF[getMatchRef(id).homeTeam()]++;
        outcome.makeSF[getMatchRef(id).awayTeam()]++;

        runKnockoutMatch(id, sfWinners[id - 101], sfLosers[id - 101]);
    }

    // Set Third Place and Final
    // Loser 101 vs Loser 102
    getMatchRef(103).setTeams(sfLosers[0], sfLosers[1]);
    // Winner 101 vs Winner 102
    getMatchRef(104).setTeams(sfWinners[0], sfWinners[1]);

    // --- Third Place Play-off (Match 103) ---
    std::string tpWinner, tpLoser;
    runKnockoutMatch(103, tpWinner, tpLoser);

    // --- Final (Match 104) ---
    std::string champion, runnerUp;
    outcome.makeFinal[getMatchRef(104).homeTeam()]++;
    outcome.makeFinal[getMatchRef(104).awayTeam()]++;

    runKnockoutMatch(104, champion, runnerUp);

    outcome.winChampion[champion]++;

    return outcome;
}

MatchSimulationResults MonteCarlo::simulate(const Tournament& tournament,
                                            int iterations,
                                            unsigned int seed) {
    MatchSimulationResults results;
    results.totalIterations = iterations;

    // Initialize maps
    for (const auto& [abbr, team] : tournament.allTeams()) {
        results.group1stProbability[abbr] = 0.0;
        results.group2ndProbability[abbr] = 0.0;
        results.group3rdProbability[abbr] = 0.0;
        results.group4thProbability[abbr] = 0.0;
        results.r32Probability[abbr] = 0.0;
        results.r16Probability[abbr] = 0.0;
        results.qfProbability[abbr] = 0.0;
        results.sfProbability[abbr] = 0.0;
        results.finalProbability[abbr] = 0.0;
        results.championProbability[abbr] = 0.0;
        results.expectedPoints[abbr] = 0.0;
    }

    TeamOutcome aggregate;

    #ifdef _OPENMP
    #pragma omp parallel
    {
        const int tid = omp_get_thread_num();
        std::mt19937 threadRng;
        if (seed == 0) {
            std::random_device rd;
            threadRng.seed(rd() ^ (static_cast<unsigned>(tid) * 2654435761u));
        } else {
            threadRng.seed(seed + static_cast<unsigned>(tid));
        }

        TeamOutcome local;

        #pragma omp for schedule(static)
        for (int i = 0; i < iterations; ++i) {
            TeamOutcome outcome = simulateIteration(tournament, threadRng);
            for (const auto& [abbr, count] : outcome.group1st) local.group1st[abbr] += count;
            for (const auto& [abbr, count] : outcome.group2nd) local.group2nd[abbr] += count;
            for (const auto& [abbr, count] : outcome.group3rd) local.group3rd[abbr] += count;
            for (const auto& [abbr, count] : outcome.group4th) local.group4th[abbr] += count;
            for (const auto& [abbr, count] : outcome.makeR32) local.makeR32[abbr] += count;
            for (const auto& [abbr, count] : outcome.makeR16) local.makeR16[abbr] += count;
            for (const auto& [abbr, count] : outcome.makeQF) local.makeQF[abbr] += count;
            for (const auto& [abbr, count] : outcome.makeSF) local.makeSF[abbr] += count;
            for (const auto& [abbr, count] : outcome.makeFinal) local.makeFinal[abbr] += count;
            for (const auto& [abbr, count] : outcome.winChampion) local.winChampion[abbr] += count;
            for (const auto& [abbr, pts] : outcome.avgPoints) local.avgPoints[abbr] += pts;
        }

        #pragma omp critical
        {
            for (const auto& [abbr, count] : local.group1st) aggregate.group1st[abbr] += count;
            for (const auto& [abbr, count] : local.group2nd) aggregate.group2nd[abbr] += count;
            for (const auto& [abbr, count] : local.group3rd) aggregate.group3rd[abbr] += count;
            for (const auto& [abbr, count] : local.group4th) aggregate.group4th[abbr] += count;
            for (const auto& [abbr, count] : local.makeR32) aggregate.makeR32[abbr] += count;
            for (const auto& [abbr, count] : local.makeR16) aggregate.makeR16[abbr] += count;
            for (const auto& [abbr, count] : local.makeQF) aggregate.makeQF[abbr] += count;
            for (const auto& [abbr, count] : local.makeSF) aggregate.makeSF[abbr] += count;
            for (const auto& [abbr, count] : local.makeFinal) aggregate.makeFinal[abbr] += count;
            for (const auto& [abbr, count] : local.winChampion) aggregate.winChampion[abbr] += count;
            for (const auto& [abbr, pts] : local.avgPoints) aggregate.avgPoints[abbr] += pts;
        }
    }
    #else
    if (seed == 0) {
        rng_.seed(std::random_device{}());
    } else {
        rng_.seed(seed);
    }
    for (int i = 0; i < iterations; ++i) {
        TeamOutcome outcome = simulateIteration(tournament, rng_);
        for (const auto& [abbr, count] : outcome.group1st) aggregate.group1st[abbr] += count;
        for (const auto& [abbr, count] : outcome.group2nd) aggregate.group2nd[abbr] += count;
        for (const auto& [abbr, count] : outcome.group3rd) aggregate.group3rd[abbr] += count;
        for (const auto& [abbr, count] : outcome.group4th) aggregate.group4th[abbr] += count;
        for (const auto& [abbr, count] : outcome.makeR32) aggregate.makeR32[abbr] += count;
        for (const auto& [abbr, count] : outcome.makeR16) aggregate.makeR16[abbr] += count;
        for (const auto& [abbr, count] : outcome.makeQF) aggregate.makeQF[abbr] += count;
        for (const auto& [abbr, count] : outcome.makeSF) aggregate.makeSF[abbr] += count;
        for (const auto& [abbr, count] : outcome.makeFinal) aggregate.makeFinal[abbr] += count;
        for (const auto& [abbr, count] : outcome.winChampion) aggregate.winChampion[abbr] += count;
        for (const auto& [abbr, pts] : outcome.avgPoints) aggregate.avgPoints[abbr] += pts;
    }
    #endif

    // Compute final probabilities
    for (const auto& [abbr, team] : tournament.allTeams()) {
        results.group1stProbability[abbr] = static_cast<double>(aggregate.group1st[abbr]) / iterations;
        results.group2ndProbability[abbr] = static_cast<double>(aggregate.group2nd[abbr]) / iterations;
        results.group3rdProbability[abbr] = static_cast<double>(aggregate.group3rd[abbr]) / iterations;
        results.group4thProbability[abbr] = static_cast<double>(aggregate.group4th[abbr]) / iterations;
        results.r32Probability[abbr] = static_cast<double>(aggregate.makeR32[abbr]) / iterations;
        results.r16Probability[abbr] = static_cast<double>(aggregate.makeR16[abbr]) / iterations;
        results.qfProbability[abbr] = static_cast<double>(aggregate.makeQF[abbr]) / iterations;
        results.sfProbability[abbr] = static_cast<double>(aggregate.makeSF[abbr]) / iterations;
        results.finalProbability[abbr] = static_cast<double>(aggregate.makeFinal[abbr]) / iterations;
        results.championProbability[abbr] = static_cast<double>(aggregate.winChampion[abbr]) / iterations;
        results.expectedPoints[abbr] = static_cast<double>(aggregate.avgPoints[abbr]) / iterations;
    }

    results.outcomes = aggregate;
    return results;
}

ImpactAnalysisResults MonteCarlo::analyzeImpact(const Tournament& tournament,
                                                int iterations,
                                                unsigned int seed) {
    ImpactAnalysisResults results;
    
    // Find next unplayed group stage matches
    // For World Cup, group matches are matches 1 to 72.
    std::vector<Match> nextGroupMatches;
    for (const auto& match : tournament.allMatches()) {
        if (match.stage() == "group" && !match.isPlayed()) {
            nextGroupMatches.push_back(match);
        }
    }

    if (nextGroupMatches.empty()) {
        return results;
    }

    // Sort to prioritize earlier matches (optional)
    const auto baseline = simulate(tournament, iterations, seed);

    for (const auto& match : nextGroupMatches) {
        // Force home win
        Tournament homeWinTour = tournament;
        for (auto& m : homeWinTour.allMatches()) {
            if (m.matchId() == match.matchId()) {
                m.setScore(2, 0, -1, -1, "final"); // Force 2-0 home win
                break;
            }
        }
        homeWinTour.computeStandings();
        const auto homeWinResults = simulate(homeWinTour, iterations, seed);

        // Force away win
        Tournament awayWinTour = tournament;
        for (auto& m : awayWinTour.allMatches()) {
            if (m.matchId() == match.matchId()) {
                m.setScore(0, 2, -1, -1, "final"); // Force 0-2 away win
                break;
            }
        }
        awayWinTour.computeStandings();
        const auto awayWinResults = simulate(awayWinTour, iterations, seed);

        GameImpact impact{};
        impact.matchId = match.matchId();
        impact.date = match.date();
        impact.homeTeam = match.homeTeam();
        impact.awayTeam = match.awayTeam();
        
        impact.homeR32IfHomeWins = homeWinResults.r32Probability.at(match.homeTeam());
        impact.awayR32IfAwayWins = awayWinResults.r32Probability.at(match.awayTeam());

        impact.homeDeltaR32 = impact.homeR32IfHomeWins - baseline.r32Probability.at(match.homeTeam());
        impact.awayDeltaR32 = impact.awayR32IfAwayWins - baseline.r32Probability.at(match.awayTeam());

        results.gameImpacts.push_back(impact);
    }

    // Sort by combined absolute impact
    std::sort(results.gameImpacts.begin(), results.gameImpacts.end(),
              [](const GameImpact& a, const GameImpact& b) {
                  double magA = std::abs(a.homeDeltaR32) + std::abs(a.awayDeltaR32);
                  double magB = std::abs(b.homeDeltaR32) + std::abs(b.awayDeltaR32);
                  return magA > magB;
              });

    return results;
}

void MonteCarlo::setModelParameters(double baseRate, double alpha, double hostAdvantage) {
    baseRate_ = std::max(0.1, baseRate);
    alpha_ = alpha;
    hostAdvantage_ = hostAdvantage;
}

bool MonteCarlo::loadModelParameters(const std::string& path) {
    try {
        const auto data = CsvParser::parse(path);
        for (const auto& row : data) {
            const std::string& key = row.at("coefficient");
            const std::string& val = row.at("value");
            if (key == "base_rate") baseRate_ = std::stod(val);
            else if (key == "alpha") alpha_ = std::stod(val);
            else if (key == "host_advantage") hostAdvantage_ = std::stod(val);
        }
        return true;
    } catch (...) {
        return false;
    }
}

void MonteCarlo::saveModelParameters(const std::string& path) const {
    CsvParser::Table table;
    table.push_back({{"coefficient", "base_rate"}, {"value", std::to_string(baseRate_)}});
    table.push_back({{"coefficient", "alpha"}, {"value", std::to_string(alpha_)}});
    table.push_back({{"coefficient", "host_advantage"}, {"value", std::to_string(hostAdvantage_)}});
    CsvParser::write(path, {"coefficient", "value"}, table);
}

// Optimization algorithm for fitting Poisson model:
// Uses past_world_cups.csv with schema: year,stage,home_team,away_team,home_score,away_score,home_elo,away_elo,is_host_home,is_host_away
FitResult fitPoissonModel(const std::string& historicalCsvPath) {
    const auto data = CsvParser::parse(historicalCsvPath);

    struct FitSample {
        double eloDiff;
        int homeScore;
        int awayScore;
        double isHostHome;
        double isHostAway;
    };

    std::vector<FitSample> samples;
    for (const auto& row : data) {
        try {
            double homeElo = std::stod(row.at("home_elo"));
            double awayElo = std::stod(row.at("away_elo"));
            int homeScore = std::stoi(row.at("home_score"));
            int awayScore = std::stoi(row.at("away_score"));
            double isHostHome = std::stod(row.at("is_host_home"));
            double isHostAway = std::stod(row.at("is_host_away"));

            samples.push_back({
                homeElo - awayElo,
                homeScore,
                awayScore,
                isHostHome,
                isHostAway
            });
        } catch (...) {
            continue;
        }
    }

    if (samples.empty()) {
        throw std::runtime_error("No samples found for historical calibration.");
    }

    // Gradient descent optimization
    // We parameterize lambda_home = exp(b + w * eloDiff + h * isHostHome)
    // and lambda_away = exp(b - w * eloDiff + h * isHostAway)
    // NLL = sum (lambda - score * ln(lambda))
    double b = std::log(1.35); // initial base_rate = 1.35
    double w = 0.0016;         // initial alpha = 0.0016
    double h = 0.18;           // initial host_advantage = 0.18

    const double lr = 0.005;
    const double invN = 1.0 / samples.size();

    for (int iter = 0; iter < 2500; ++iter) {
        double gradB = 0.0;
        double gradW = 0.0;
        double gradH = 0.0;

        for (const auto& sample : samples) {
            double lamH = std::exp(b + w * sample.eloDiff + h * sample.isHostHome);
            double lamA = std::exp(b - w * sample.eloDiff + h * sample.isHostAway);

            // derivatives for home goals
            gradB += (lamH - sample.homeScore);
            gradW += (lamH - sample.homeScore) * sample.eloDiff;
            gradH += (lamH - sample.homeScore) * sample.isHostHome;

            // derivatives for away goals
            gradB += (lamA - sample.awayScore);
            gradW -= (lamA - sample.awayScore) * sample.eloDiff;
            gradH += (lamA - sample.awayScore) * sample.isHostAway;
        }

        b -= lr * gradB * invN;
        w -= lr * gradW * invN * 0.0001; // scale Elo difference gradient to keep it stable
        h -= lr * gradH * invN;
    }

    // Compute final NLL
    double finalNll = 0.0;
    for (const auto& sample : samples) {
        double lamH = std::exp(b + w * sample.eloDiff + h * sample.isHostHome);
        double lamA = std::exp(b - w * sample.eloDiff + h * sample.isHostAway);
        finalNll += (lamH - sample.homeScore * std::log(std::max(1e-9, lamH)));
        finalNll += (lamA - sample.awayScore * std::log(std::max(1e-9, lamA)));
    }
    finalNll *= invN;

    return {
        std::exp(b), // baseRate
        w,           // alpha
        h,           // hostAdvantage
        finalNll
    };
}
