#include "Tiebreaker.h"
#include "Tournament.h"
#include <algorithm>
#include <map>
#include <set>
#include <iostream>

namespace {

struct H2HStats {
    int points = 0;
    int goalDiff = 0;
    int goalsFor = 0;
};

// Computes head-to-head stats among a subset of tied teams.
std::map<Team*, H2HStats> computeH2H(const std::vector<Team*>& tiedTeams, const std::vector<Match>& matches) {
    std::map<Team*, H2HStats> stats;
    std::set<std::string> tiedAbbrs;
    for (auto* t : tiedTeams) {
        tiedAbbrs.insert(t->abbreviation());
        stats[t] = H2HStats{};
    }

    for (const auto& match : matches) {
        if (match.stage() == "group" && match.isPlayed()) {
            if (tiedAbbrs.count(match.homeTeam()) && tiedAbbrs.count(match.awayTeam())) {
                Team* home = nullptr;
                Team* away = nullptr;
                for (auto* t : tiedTeams) {
                    if (t->abbreviation() == match.homeTeam()) home = t;
                    if (t->abbreviation() == match.awayTeam()) away = t;
                }
                if (home && away) {
                    stats[home].goalsFor += match.homeScore();
                    stats[home].goalDiff += (match.homeScore() - match.awayScore());
                    stats[away].goalsFor += match.awayScore();
                    stats[away].goalDiff += (match.awayScore() - match.homeScore());

                    if (match.homeScore() > match.awayScore()) {
                        stats[home].points += 3;
                    } else if (match.homeScore() < match.awayScore()) {
                        stats[away].points += 3;
                    } else {
                        stats[home].points += 1;
                        stats[away].points += 1;
                    }
                }
            }
        }
    }
    return stats;
}

// Allowed third-place group sets for the 8 group winners in Round of 32
const std::vector<std::string> winners = {"E", "I", "A", "L", "G", "D", "B", "K"};
const std::map<std::string, std::set<std::string>> allowedGroups = {
    {"E", {"A", "B", "C", "D", "F"}},
    {"I", {"C", "D", "F", "G", "H"}},
    {"A", {"C", "E", "F", "H", "I"}},
    {"L", {"E", "H", "I", "J", "K"}},
    {"G", {"A", "E", "H", "I", "J"}},
    {"D", {"B", "E", "F", "I", "J"}},
    {"B", {"E", "F", "G", "I", "J"}},
    {"K", {"D", "E", "I", "J", "L"}}
};

bool backtrackMatchups(size_t winnerIdx,
                       const std::vector<Team*>& bestThirdPlaces,
                       std::vector<bool>& used,
                       std::vector<size_t>& assignment) {
    if (winnerIdx == winners.size()) {
        return true;
    }

    const std::string& winGroup = winners[winnerIdx];
    const auto& allowed = allowedGroups.at(winGroup);

    for (size_t j = 0; j < bestThirdPlaces.size(); ++j) {
        if (!used[j]) {
            const std::string& thirdGroup = bestThirdPlaces[j]->group();
            // Group winners cannot play a third place team from their own group
            if (thirdGroup != winGroup && allowed.count(thirdGroup)) {
                used[j] = true;
                assignment[winnerIdx] = j;

                if (backtrackMatchups(winnerIdx + 1, bestThirdPlaces, used, assignment)) {
                    return true;
                }

                used[j] = false;
            }
        }
    }
    return false;
}

} // namespace

std::vector<Team*> Tiebreaker::breakGroupTie(std::vector<Team*>& teams,
                                            const std::vector<Match>& matches) {
    if (teams.size() <= 1) {
        return teams;
    }

    // Sort primarily by points
    std::sort(teams.begin(), teams.end(), [](const Team* a, const Team* b) {
        return a->points() > b->points();
    });

    // Check for ties and apply H2H then overall stats
    std::vector<Team*> finalSorted;
    size_t i = 0;
    while (i < teams.size()) {
        size_t j = i + 1;
        while (j < teams.size() && teams[i]->points() == teams[j]->points()) {
            j++;
        }

        std::vector<Team*> tied(teams.begin() + i, teams.begin() + j);
        if (tied.size() > 1) {
            // Compute head-to-head records among tied teams
            auto h2h = computeH2H(tied, matches);
            std::sort(tied.begin(), tied.end(), [&h2h](Team* a, Team* b) {
                const auto& sa = h2h[a];
                const auto& sb = h2h[b];
                
                // 1. Head-to-head points
                if (sa.points != sb.points) return sa.points > sb.points;
                // 2. Head-to-head goal difference
                if (sa.goalDiff != sb.goalDiff) return sa.goalDiff > sb.goalDiff;
                // 3. Head-to-head goals scored
                if (sa.goalsFor != sb.goalsFor) return sa.goalsFor > sb.goalsFor;
                
                // 4. Overall goal difference
                if (a->goalDifference() != b->goalDifference()) return a->goalDifference() > b->goalDifference();
                // 5. Overall goals scored
                if (a->goalsFor() != b->goalsFor()) return a->goalsFor() > b->goalsFor();
                
                // 6. Fallback: drawing of lots (alphabetical by abbreviation)
                return a->abbreviation() < b->abbreviation();
            });
        }

        finalSorted.insert(finalSorted.end(), tied.begin(), tied.end());
        i = j;
    }

    return finalSorted;
}

std::vector<Team*> Tiebreaker::rankThirdPlaces(const std::vector<Team*>& thirdPlaces) {
    std::vector<Team*> sorted = thirdPlaces;
    std::sort(sorted.begin(), sorted.end(), [](const Team* a, const Team* b) {
        if (a->points() != b->points()) return a->points() > b->points();
        if (a->goalDifference() != b->goalDifference()) return a->goalDifference() > b->goalDifference();
        if (a->goalsFor() != b->goalsFor()) return a->goalsFor() > b->goalsFor();
        // Fallback: alphabetical
        return a->abbreviation() < b->abbreviation();
    });
    return sorted;
}

void Tiebreaker::allocateRoundOf32Matchups(Tournament& tournament) {
    // 1. Get Group winners and runners-up
    std::vector<std::string> groupsList = {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L"};
    std::map<std::string, std::vector<Team*>> groupStandings;

    for (const auto& group : groupsList) {
        groupStandings[group] = tournament.teamsByGroup(group);
    }

    // 2. Identify third-place teams
    std::vector<Team*> thirdPlaces;
    for (const auto& group : groupsList) {
        if (groupStandings[group].size() >= 3) {
            thirdPlaces.push_back(groupStandings[group][2]);
        }
    }

    // 3. Rank third-place teams and select the top 8
    std::vector<Team*> rankedThirds = rankThirdPlaces(thirdPlaces);
    std::vector<Team*> bestThirds(rankedThirds.begin(), rankedThirds.begin() + 8);

    // 4. Run backtracking to pair the 8 best third-place teams to the 8 group winners
    std::vector<bool> used(bestThirds.size(), false);
    std::vector<size_t> assignment(winners.size(), 0);

    bool matched = backtrackMatchups(0, bestThirds, used, assignment);
    if (!matched) {
        // Fallback in case of constraint failure (should never happen with valid settings)
        std::cerr << "Warning: Could not find valid third-place assignment matching FIFA rules." << std::endl;
        for (size_t i = 0; i < winners.size(); ++i) {
            assignment[i] = i % bestThirds.size();
        }
    }

    // Map winners -> assigned third place team abbreviation
    std::map<std::string, std::string> winnerToThirdPlace;
    for (size_t i = 0; i < winners.size(); ++i) {
        winnerToThirdPlace[winners[i]] = bestThirds[assignment[i]]->abbreviation();
    }

    // 5. Update matches in schedule.csv (Round of 32 has match IDs 73 to 88)
    auto& matches = tournament.allMatches();
    for (auto& match : matches) {
        if (match.stage() == "knockout_r32") {
            int id = match.matchId();
            std::string home = "TBD";
            std::string away = "TBD";

            // Bracket mappings (Match 73 to 88)
            switch (id) {
                case 73: // Runner-up Group A vs. Runner-up Group B
                    home = groupStandings["A"][1]->abbreviation();
                    away = groupStandings["B"][1]->abbreviation();
                    break;
                case 74: // Winner Group E vs. 3rd Group A/B/C/D/F
                    home = groupStandings["E"][0]->abbreviation();
                    away = winnerToThirdPlace["E"];
                    break;
                case 75: // Winner Group F vs. Runner-up Group C
                    home = groupStandings["F"][0]->abbreviation();
                    away = groupStandings["C"][1]->abbreviation();
                    break;
                case 76: // Winner Group C vs. Runner-up Group F
                    home = groupStandings["C"][0]->abbreviation();
                    away = groupStandings["F"][1]->abbreviation();
                    break;
                case 77: // Winner Group I vs. 3rd Group C/D/F/G/H
                    home = groupStandings["I"][0]->abbreviation();
                    away = winnerToThirdPlace["I"];
                    break;
                case 78: // Runner-up Group E vs. Runner-up Group I
                    home = groupStandings["E"][1]->abbreviation();
                    away = groupStandings["I"][1]->abbreviation();
                    break;
                case 79: // Winner Group A vs. 3rd Group C/E/F/H/I
                    home = groupStandings["A"][0]->abbreviation();
                    away = winnerToThirdPlace["A"];
                    break;
                case 80: // Winner Group L vs. 3rd Group E/H/I/J/K
                    home = groupStandings["L"][0]->abbreviation();
                    away = winnerToThirdPlace["L"];
                    break;
                case 81: // Winner Group D vs. 3rd Group B/E/F/I/J
                    home = groupStandings["D"][0]->abbreviation();
                    away = winnerToThirdPlace["D"];
                    break;
                case 82: // Winner Group G vs. 3rd Group A/E/H/I/J
                    home = groupStandings["G"][0]->abbreviation();
                    away = winnerToThirdPlace["G"];
                    break;
                case 83: // Runner-up Group K vs. Runner-up Group L
                    home = groupStandings["K"][1]->abbreviation();
                    away = groupStandings["L"][1]->abbreviation();
                    break;
                case 84: // Winner Group H vs. Runner-up Group J
                    home = groupStandings["H"][0]->abbreviation();
                    away = groupStandings["J"][1]->abbreviation();
                    break;
                case 85: // Winner Group B vs. 3rd Group E/F/G/I/J
                    home = groupStandings["B"][0]->abbreviation();
                    away = winnerToThirdPlace["B"];
                    break;
                case 86: // Winner Group J vs. Runner-up Group H
                    home = groupStandings["J"][0]->abbreviation();
                    away = groupStandings["H"][1]->abbreviation();
                    break;
                case 87: // Winner Group K vs. 3rd Group D/E/I/J/L
                    home = groupStandings["K"][0]->abbreviation();
                    away = winnerToThirdPlace["K"];
                    break;
                case 88: // Runner-up Group D vs. Runner-up Group G
                    home = groupStandings["D"][1]->abbreviation();
                    away = groupStandings["G"][1]->abbreviation();
                    break;
                default:
                    break;
            }

            match.setTeams(home, away);
        }
    }
}
