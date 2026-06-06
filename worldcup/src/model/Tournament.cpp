#include "Tournament.h"
#include "util/CsvParser.h"
#include "Tiebreaker.h"
#include <set>

void Tournament::addTeam(const Team& team) {
    teams_[team.abbreviation()] = team;
}

Team* Tournament::getTeam(const std::string& abbreviation) {
    auto it = teams_.find(abbreviation);
    if (it != teams_.end()) {
        return &(it->second);
    }
    return nullptr;
}

const Team* Tournament::getTeam(const std::string& abbreviation) const {
    auto it = teams_.find(abbreviation);
    if (it != teams_.end()) {
        return &(it->second);
    }
    return nullptr;
}

void Tournament::addMatch(const Match& match) {
    matches_.push_back(match);
}

void Tournament::computeStandings() {
    for (auto& [abbr, team] : teams_) {
        team.resetRecord();
    }
    for (const auto& match : matches_) {
        if (match.stage() == "group" && match.isPlayed()) {
            Team* home = getTeam(match.homeTeam());
            Team* away = getTeam(match.awayTeam());
            if (home && away) {
                home->addGoalsFor(match.homeScore());
                home->addGoalsAgainst(match.awayScore());
                away->addGoalsFor(match.awayScore());
                away->addGoalsAgainst(match.homeScore());

                if (match.homeScore() > match.awayScore()) {
                    home->addWin();
                    away->addLoss();
                } else if (match.homeScore() < match.awayScore()) {
                    home->addLoss();
                    away->addWin();
                } else {
                    home->addDraw();
                    away->addDraw();
                }
            }
        }
    }
}

std::vector<Team*> Tournament::teamsByGroup(const std::string& groupName) {
    std::vector<Team*> groupTeams;
    for (auto& [abbr, team] : teams_) {
        if (team.group() == groupName) {
            groupTeams.push_back(&team);
        }
    }
    // Apply full FIFA group tiebreaker to sort them
    return Tiebreaker::breakGroupTie(groupTeams, matches_);
}

std::vector<const Team*> Tournament::teamsByGroup(const std::string& groupName) const {
    std::vector<const Team*> groupTeams;
    for (const auto& [abbr, team] : teams_) {
        if (team.group() == groupName) {
            groupTeams.push_back(&team);
        }
    }
    // Cast and sort using Tiebreaker
    std::vector<Team*> tempTeams;
    for (const auto* t : groupTeams) {
        tempTeams.push_back(const_cast<Team*>(t));
    }
    auto sorted = Tiebreaker::breakGroupTie(tempTeams, matches_);
    std::vector<const Team*> result;
    for (const auto* t : sorted) {
        result.push_back(t);
    }
    return result;
}

std::vector<std::string> Tournament::getGroups() const {
    std::set<std::string> groupNames;
    for (const auto& [abbr, team] : teams_) {
        groupNames.insert(team.group());
    }
    return std::vector<std::string>(groupNames.begin(), groupNames.end());
}

namespace wc {

Tournament loadTournamentFromCsvFiles(const std::string& teamsPath, const std::string& schedulePath) {
    Tournament tournament;

    // Load teams
    const auto teamsData = CsvParser::parse(teamsPath);
    for (const auto& row : teamsData) {
        double elo = 1500.0;
        try {
            elo = std::stod(row.at("elo_rating"));
        } catch (...) {}

        Team team(row.at("abbreviation"),
                  row.at("full_name"),
                  row.at("group"),
                  elo,
                  row.at("federation"));
        tournament.addTeam(team);
    }

    // Load matches
    const auto scheduleData = CsvParser::parse(schedulePath);
    for (const auto& row : scheduleData) {
        int matchId = std::stoi(row.at("match_id"));
        int homeScore = std::stoi(row.at("home_score"));
        int awayScore = std::stoi(row.at("away_score"));
        int homePenalty = std::stoi(row.at("home_penalty_score"));
        int awayPenalty = std::stoi(row.at("away_penalty_score"));

        std::string hostCity = "";
        try {
            hostCity = row.at("host_city");
        } catch (...) {}

        Match match(matchId,
                    row.at("stage"),
                    row.at("group"),
                    row.at("date"),
                    row.at("home_team"),
                    row.at("away_team"),
                    homeScore,
                    awayScore,
                    homePenalty,
                    awayPenalty,
                    row.at("status"),
                    hostCity);
        tournament.addMatch(match);
    }

    return tournament;
}

} // namespace wc
