#include "AsciiPrinter.h"
#include "model/Tiebreaker.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <set>
#include <cmath>
#include <random>
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

static std::string formatTeamName(const std::string& teamCode) {
    if (teamCode.empty()) return "";
    if (teamCode.find("WINNER_GROUP_") == 0) return "1" + teamCode.substr(13);
    if (teamCode.find("RUNNER_UP_GROUP_") == 0) return "2" + teamCode.substr(16);
    if (teamCode.find("THIRD_GROUP_") == 0) return "3rd";
    if (teamCode.find("R32_WINNER_") == 0) return "W" + teamCode.substr(11);
    if (teamCode.find("R16_WINNER_") == 0) return "W" + teamCode.substr(11);
    if (teamCode.find("QUARTERFINAL_WINNER_") == 0) return "W" + teamCode.substr(20);
    if (teamCode.find("SEMIFINAL_WINNER_") == 0) return "W" + teamCode.substr(17);
    if (teamCode.find("TBD_") == 0) return "TBD";
    if (teamCode.length() > 4) return teamCode.substr(0, 3);
    return teamCode;
}

static std::string padRight(const std::string& str, size_t len) {
    if (str.length() >= len) return str.substr(0, len);
    return str + std::string(len - str.length(), ' ');
}

static double computeWinProb(const Team& home, const Team& away, const MonteCarlo* mc) {
    if (!mc) return 0.5;
    double eloDiff = home.eloRating() - away.eloRating();
    double homeBoost = MonteCarlo::isHost(home.abbreviation()) ? mc->hostAdvantage() : 0.0;
    double awayBoost = MonteCarlo::isHost(away.abbreviation()) ? mc->hostAdvantage() : 0.0;

    double lambdaHome = mc->baseRate() * std::exp(mc->alpha() * eloDiff + homeBoost);
    double lambdaAway = mc->baseRate() * std::exp(-mc->alpha() * eloDiff + awayBoost);

    std::mt19937 rng(12345); 
    std::poisson_distribution<int> homeDist(lambdaHome);
    std::poisson_distribution<int> awayDist(lambdaAway);
    std::poisson_distribution<int> homeETDist(lambdaHome / 3.0);
    std::poisson_distribution<int> awayETDist(lambdaAway / 3.0);
    std::uniform_real_distribution<> coin(0.0, 1.0);

    int homeWins = 0;
    const int runs = 10000;
    for (int i = 0; i < runs; ++i) {
        int hs = homeDist(rng);
        int as = awayDist(rng);
        if (hs > as) {
            homeWins++;
        } else if (hs < as) {
            // away wins
        } else {
            hs += homeETDist(rng);
            as += awayETDist(rng);
            if (hs > as) homeWins++;
            else if (hs == as) {
                if (coin(rng) > 0.5) homeWins++;
            }
        }
    }
    return (double)homeWins / runs;
}

void AsciiPrinter::printBracket(const Tournament& tournament, bool unplayedOnly, const MonteCarlo* mc) {
    std::vector<std::vector<int>> rounds = {
        {74, 77, 73, 75, 83, 84, 81, 82, 76, 78, 79, 80, 86, 88, 85, 87}, // R32
        {89, 90, 93, 94, 91, 92, 95, 96}, // R16
        {97, 98, 99, 100}, // QF
        {101, 102}, // SF
        {104} // Final
    };

    auto matchesList = tournament.allMatches();
    std::map<int, Match> matchMap;
    for (const auto& m : matchesList) {
        matchMap[m.matchId()] = m;
    }

    std::vector<std::vector<std::string>> grid(63, std::vector<std::string>(6, "            ")); // 63 lines, 6 cols, 12 spaces

    for (size_t r = 0; r < rounds.size(); ++r) {
        for (size_t i = 0; i < rounds[r].size(); ++i) {
            int matchId = rounds[r][i];
            auto it = matchMap.find(matchId);
            if (it == matchMap.end()) continue;
            const Match& match = it->second;

            if (unplayedOnly && match.isFinal()) {
                continue; // Skip drawing this match and its backward connectors
            }

            int home_idx = 2 * i;
            int away_idx = 2 * i + 1;

            int L_home = home_idx * (1 << (r + 1)) + (1 << r) - 1;
            int L_away = away_idx * (1 << (r + 1)) + (1 << r) - 1;

            std::string teamStrHome = formatTeamName(match.homeTeam());
            std::string teamStrAway = formatTeamName(match.awayTeam());

            if (r == 4 && teamStrHome == "TBD") teamStrHome = "W101";
            if (r == 4 && teamStrAway == "TBD") teamStrAway = "W102";

            grid[L_home][r] = " " + padRight(teamStrHome, 6) + " ──┐ ";
            grid[L_away][r] = " " + padRight(teamStrAway, 6) + " ──┘ ";

            int L_mid = (L_home + L_away) / 2;
            std::string midStr = "          ├─";
            
            if (!match.isPlayed() && mc) {
                const Team* homeTeamPtr = tournament.getTeam(match.homeTeam());
                const Team* awayTeamPtr = tournament.getTeam(match.awayTeam());
                if (homeTeamPtr && awayTeamPtr) {
                    double homeWinProb = computeWinProb(*homeTeamPtr, *awayTeamPtr, mc);
                    int homeP = std::round(homeWinProb * 100);
                    int awayP = 100 - homeP;
                    std::string probStr = std::to_string(homeP) + "/" + std::to_string(awayP) + "%";
                    midStr = " " + padRight(probStr, 9) + "├─";
                }
            }

            for (int L = L_home + 1; L < L_away; ++L) {
                if (L == L_mid) {
                    grid[L][r] = midStr;
                } else {
                    grid[L][r] = "          │ ";
                }
            }

            // Print champion if final match is played and not hidden
            if (r == 4 && match.isFinal() && !unplayedOnly) {
                std::string winner = match.homeTeamWon() ? match.homeTeam() : match.awayTeam();
                grid[L_mid][5] = " " + padRight(formatTeamName(winner), 11);
            }
        }
    }

    std::cout << "\n";
    std::cout << "\033[1mROUND OF 32 ROUND OF 16 QUARTERFINALSEMIFINALS  FINAL       CHAMPION    \033[0m\n";
    std::cout << "─────────── ─────────── ─────────── ─────────── ─────────── ─────────── \n";

    for (int L = 0; L < 63; ++L) {
        bool emptyLine = true;
        bool onlySpacesAndPipes = true;
        std::string lineOutput;
        for (int c = 0; c < 6; ++c) {
            if (grid[L][c] != "            ") emptyLine = false;
            if (grid[L][c] != "            " && grid[L][c] != "          │ ") {
                onlySpacesAndPipes = false;
            }
            lineOutput += grid[L][c];
        }

        if (unplayedOnly && onlySpacesAndPipes) {
            continue;
        }

        if (!emptyLine) {
            std::cout << lineOutput << "\n";
        } else {
            // Check if entirely empty lines can be skipped, but to maintain visual tree, we should print them
            // Actually, we must print empty lines if they are within the bracket range to preserve vertical scale.
            std::cout << "\n";
        }
    }
    std::cout << "\n";
}

void AsciiPrinter::printSimulationResults(const MatchSimulationResults& results) {
    struct StageInfo {
        std::string header;
        const std::map<std::string, double>* probs;
    };
    std::vector<StageInfo> stages = {
        {"Adv R32 ", &results.r32Probability},
        {"ReachR16", &results.r16Probability},
        {"ReachQF ", &results.qfProbability},
        {"ReachSF ", &results.sfProbability},
        {"ReachF  ", &results.finalProbability},
        {"Champ   ", &results.championProbability}
    };

    auto isStageDone = [](const std::map<std::string, double>& probs) {
        for (const auto& [abbr, p] : probs) {
            if (p > 1e-6 && p < 1.0 - 1e-6) return false;
        }
        return true;
    };

    std::vector<bool> stageDone(stages.size());
    int lastDoneStageIdx = -1;
    for (size_t i = 0; i < stages.size(); ++i) {
        stageDone[i] = isStageDone(*stages[i].probs);
        if (stageDone[i]) {
            lastDoneStageIdx = static_cast<int>(i);
        } else {
            break;
        }
    }

    std::vector<StageInfo> colsToShow;
    for (size_t i = 0; i < stages.size(); ++i) {
        if (!stageDone[i]) {
            colsToShow.push_back(stages[i]);
        }
    }
    if (colsToShow.empty()) {
        colsToShow.push_back(stages.back()); // Always show Champ if all done
    }

    std::vector<std::pair<std::string, double>> activeTeams;
    for (const auto& [abbr, prob] : results.championProbability) {
        bool alive = false;
        if (lastDoneStageIdx == -1) {
            alive = results.r32Probability.at(abbr) > 1e-6;
        } else {
            alive = (*stages[lastDoneStageIdx].probs).at(abbr) >= 1.0 - 1e-6;
        }
        if (alive) {
            activeTeams.push_back({abbr, prob});
        }
    }

    // Sort by Champion odds, then the next unresolved stage odds
    std::sort(activeTeams.begin(), activeTeams.end(), [&](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        const auto& nextProbs = *colsToShow.front().probs;
        return nextProbs.at(a.first) > nextProbs.at(b.first);
    });

    std::string topBorder = "┌──────────";
    std::string midBorder = "├──────────";
    std::string botBorder = "└──────────";
    std::string headerStr = "│ Team     ";

    for (size_t i = 0; i < colsToShow.size(); ++i) {
        topBorder += "┬──────────";
        midBorder += "┼──────────";
        botBorder += "┴──────────";
        std::string h = colsToShow[i].header;
        while (h.length() < 8) h += " ";
        headerStr += "│ " + h.substr(0, 8) + " ";
    }
    topBorder += "┐";
    midBorder += "┤";
    botBorder += "┘";
    headerStr += "│";

    int innerWidth = 10 + colsToShow.size() * 11;
    std::string title = "TOURNAMENT FORECAST PROBABILITIES";
    int padding = innerWidth - 2 - title.length();
    if (padding < 0) padding = 0;
    std::string titleLine = "│ " + title + std::string(padding, ' ') + " │";

    std::cout << topBorder << "\n";
    std::cout << titleLine << "\n";
    std::cout << midBorder << "\n";
    std::cout << headerStr << "\n";
    std::cout << midBorder << "\n";

    for (const auto& [abbr, champProb] : activeTeams) {
        std::cout << "│ " << std::left << std::setw(8) << abbr;
        for (size_t i = 0; i < colsToShow.size(); ++i) {
            double val = (*colsToShow[i].probs).at(abbr);
            std::cout << " │ " << std::right << std::setprecision(1) << std::fixed
                      << std::setw(7) << val * 100.0 << "%";
        }
        std::cout << " │\n";
    }
    std::cout << botBorder << "\n";
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
