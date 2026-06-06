#ifndef TOURNAMENT_H
#define TOURNAMENT_H

#include "Team.h"
#include "Match.h"
#include <map>
#include <vector>
#include <string>

class Tournament {
public:
    Tournament() = default;

    // Team management
    void addTeam(const Team& team);
    Team* getTeam(const std::string& abbreviation);
    const Team* getTeam(const std::string& abbreviation) const;
    const std::map<std::string, Team>& allTeams() const { return teams_; }

    // Match management
    void addMatch(const Match& match);
    const std::vector<Match>& allMatches() const { return matches_; }
    std::vector<Match>& allMatches() { return matches_; }
    void replaceMatches(const std::vector<Match>& matches) { matches_ = matches; }

    // Standings calculation
    void computeStandings();

    // Queries
    std::vector<Team*> teamsByGroup(const std::string& groupName);
    std::vector<const Team*> teamsByGroup(const std::string& groupName) const;
    std::vector<std::string> getGroups() const;

private:
    std::map<std::string, Team> teams_;
    std::vector<Match> matches_;
};

namespace wc {
    Tournament loadTournamentFromCsvFiles(const std::string& teamsPath, const std::string& schedulePath);
}

#endif // TOURNAMENT_H
