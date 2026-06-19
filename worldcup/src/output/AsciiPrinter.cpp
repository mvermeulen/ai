#include "AsciiPrinter.h"
#include "model/Tiebreaker.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <set>
void AsciiPrinter::printAllStandings(const Tournament& tournament, const MatchSimulationResults* simResults) {
    std::vector<std::string> groups = tournament.getGroups();
    std::sort(groups.begin(), groups.end());

    // Identify third-place qualifiers
    std::vector<Team*> thirdPlaces;
    for (const auto& group : groups) {
        auto standing = tournament.teamsByGroup(group);
        if (standing.size() >= 3) {
            thirdPlaces.push_back(const_cast<Team*>(standing[2]));
        }
    }
    auto rankedThirds = Tiebreaker::rankThirdPlaces(thirdPlaces);
    std::set<std::string> bestThirdsAbbr;
    for (size_t i = 0; i < 8 && i < rankedThirds.size(); ++i) {
        bestThirdsAbbr.insert(rankedThirds[i]->abbreviation());
    }

    for (const auto& group : groups) {
        std::cout << "┌──────────────────────────────────────────────┐\n";
        std::cout << "│ GROUP " << group << "                                      │\n";
        std::cout << "├──────────────┬──────┬──────┬──────┬──────────────┤\n";
        std::cout << "│ Team         │ Pts  │ GD   │ GF   │ Status       │\n";
        std::cout << "├──────────────┼──────┼──────┼──────┼──────────────┤\n";

        auto standings = tournament.teamsByGroup(group);
        for (size_t i = 0; i < standings.size(); ++i) {
            const auto* team = standings[i];
            std::string status = "Eliminated";
            if (i < 2) {
                status = "Adv (Top2)";
            } else if (i == 2 && bestThirdsAbbr.count(team->abbreviation())) {
                status = "Adv (3rd) ";
            }

            if (simResults != nullptr) {
                auto it = simResults->r32Probability.find(team->abbreviation());
                if (it != simResults->r32Probability.end()) {
                    double r32 = it->second;
                    double g1 = simResults->group1stProbability.at(team->abbreviation());
                    double g2 = simResults->group2ndProbability.at(team->abbreviation());
                    double g3 = simResults->group3rdProbability.at(team->abbreviation());

                    if (g1 >= 0.999999) {
                        status = "*CLINCH 1ST*";
                    } else if (g2 >= 0.999999) {
                        status = "*CLINCH 2ND*";
                    } else if (g3 >= 0.999999) {
                        status = "*CLINCH 3RD*";
                    } else if (g1 + g2 >= 0.999999) {
                        status = "*CLINCH TOP2";
                    } else if (r32 >= 0.999999) {
                        status = "*CLINCH R32*";
                    } else if (r32 <= 0.000001) {
                        status = "*ELIMINATED*";
                    }
                }
            }

            std::cout << "│ " << std::left << std::setw(12) << team->fullName()
                      << " │ " << std::right << std::setw(4) << team->points()
                      << " │ " << std::setw(4) << team->goalDifference()
                      << " │ " << std::setw(4) << team->goalsFor()
                      << " │ " << std::left << std::setw(12) << status << " │\n";
        }
        std::cout << "└──────────────┴──────┴──────┴──────┴──────────────┘\n\n";
    }
}

void AsciiPrinter::printSimulationResults(const MatchSimulationResults& results) {
    std::vector<std::pair<std::string, double>> sortedTeams;
    for (const auto& [abbr, prob] : results.championProbability) {
        sortedTeams.push_back({abbr, prob});
    }

    // Sort by Champion odds, then R32 odds
    std::sort(sortedTeams.begin(), sortedTeams.end(), [&](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return results.r32Probability.at(a.first) > results.r32Probability.at(b.first);
    });

    std::cout << "┌──────────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ TOURNAMENT FORECAST PROBABILITIES                                        │\n";
    std::cout << "├──────────┬──────────┬──────────┬──────────┬──────────┬──────────┬────────┤\n";
    std::cout << "│ Team     │ Adv R32  │ ReachR16 │ ReachQF  │ ReachSF  │ ReachF   │ Champ  │\n";
    std::cout << "├──────────┼──────────┼──────────┼──────────┼──────────┼──────────┼────────┤\n";

    for (const auto& [abbr, champProb] : sortedTeams) {
        std::cout << "│ " << std::left << std::setw(8) << abbr
                  << " │ " << std::right << std::setprecision(1) << std::fixed
                  << std::setw(7) << results.r32Probability.at(abbr) * 100.0 << "%"
                  << " │ " << std::setw(7) << results.r16Probability.at(abbr) * 100.0 << "%"
                  << " │ " << std::setw(7) << results.qfProbability.at(abbr) * 100.0 << "%"
                  << " │ " << std::setw(7) << results.sfProbability.at(abbr) * 100.0 << "%"
                  << " │ " << std::setw(7) << results.finalProbability.at(abbr) * 100.0 << "%"
                  << " │ " << std::setw(5) << champProb * 100.0 << "% │\n";
    }
    std::cout << "└──────────┴──────────┴──────────┴──────────┴──────────┴──────────┴────────┘\n";
}

void AsciiPrinter::printImpactAnalysis(const ImpactAnalysisResults& analysis) {
    if (analysis.gameImpacts.empty()) {
        std::cout << "No upcoming unplayed matches found for impact analysis." << std::endl;
        return;
    }

    std::cout << "┌────────────────────────────────────────────────────────────────────┐\n";
    std::cout << "│ MATCH PROGRESSION IMPORTANCE (Impact on R32 Advancement)           │\n";
    std::cout << "├────────┬────────────┬──────────────┬──────────────┬────────────────┤\n";
    std::cout << "│ Match  │ Date       │ Home Delta   │ Away Delta   │ Combined       │\n";
    std::cout << "├────────┼────────────┼──────────────┼──────────────┼────────────────┤\n";

    for (const auto& imp : analysis.gameImpacts) {
        std::string matchup = imp.homeTeam + " v " + imp.awayTeam;
        double combined = std::abs(imp.homeDeltaR32) + std::abs(imp.awayDeltaR32);
        std::cout << "│ " << std::left << std::setw(6) << imp.matchId
                  << " │ " << std::setw(10) << imp.date
                  << " │ " << std::right << std::setprecision(1) << std::fixed
                  << (imp.homeDeltaR32 >= 0 ? "+" : "") << std::setw(10) << imp.homeDeltaR32 * 100.0 << "%"
                  << " │ " << (imp.awayDeltaR32 >= 0 ? "+" : "") << std::setw(10) << imp.awayDeltaR32 * 100.0 << "%"
                  << " │ " << std::setw(12) << combined * 100.0 << "% │\n";
    }
    std::cout << "└────────┴────────────┴──────────────┴──────────────┴────────────────┘\n";
}
