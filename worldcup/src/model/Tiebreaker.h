#ifndef TIEBREAKER_H
#define TIEBREAKER_H

#include "Team.h"
#include "Match.h"
#include <vector>

class Tournament;

class Tiebreaker {
public:
    /**
     * Sorts a group's teams from 1st to 4th based on FIFA tiebreaker rules.
     */
    static std::vector<Team*> breakGroupTie(std::vector<Team*>& teams,
                                            const std::vector<Match>& matches);

    /**
     * Sorts the third-place teams from all groups to find the best 8.
     */
    static std::vector<Team*> rankThirdPlaces(const std::vector<Team*>& thirdPlaces);

    /**
     * Allocates matchups for the Round of 32 (matches 73 to 88) in the tournament.
     */
    static void allocateRoundOf32Matchups(Tournament& tournament);
};

#endif // TIEBREAKER_H
