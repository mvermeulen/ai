#include "Match.h"

Match::Match(int matchId, const std::string& stage, const std::string& group,
             const std::string& date, const std::string& homeTeam, const std::string& awayTeam,
             int homeScore, int awayScore, int homePenaltyScore, int awayPenaltyScore,
             const std::string& status, const std::string& hostCity)
    : matchId_(matchId), stage_(stage), group_(group), date_(date),
      homeTeam_(homeTeam), awayTeam_(awayTeam), homeScore_(homeScore), awayScore_(awayScore),
      homePenaltyScore_(homePenaltyScore), awayPenaltyScore_(awayPenaltyScore), status_(status),
      hostCity_(hostCity) {}
