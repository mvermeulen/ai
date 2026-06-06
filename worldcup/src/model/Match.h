#ifndef MATCH_H
#define MATCH_H

#include <string>

class Match {
public:
    Match() = default;
    Match(int matchId, const std::string& stage, const std::string& group,
          const std::string& date, const std::string& homeTeam, const std::string& awayTeam,
          int homeScore, int awayScore, int homePenaltyScore, int awayPenaltyScore,
          const std::string& status, const std::string& hostCity = "");

    // Accessors
    int matchId() const { return matchId_; }
    const std::string& stage() const { return stage_; }
    const std::string& group() const { return group_; }
    const std::string& date() const { return date_; }
    const std::string& homeTeam() const { return homeTeam_; }
    const std::string& awayTeam() const { return awayTeam_; }
    int homeScore() const { return homeScore_; }
    int awayScore() const { return awayScore_; }
    int homePenaltyScore() const { return homePenaltyScore_; }
    int awayPenaltyScore() const { return awayPenaltyScore_; }
    const std::string& status() const { return status_; }
    const std::string& hostCity() const { return hostCity_; }

    // Mutator
    void setTeams(const std::string& home, const std::string& away) {
        homeTeam_ = home;
        awayTeam_ = away;
    }
    void setScore(int homeScore, int awayScore, int homePenalty = -1, int awayPenalty = -1, const std::string& status = "final") {
        homeScore_ = homeScore;
        awayScore_ = awayScore;
        homePenaltyScore_ = homePenalty;
        awayPenaltyScore_ = awayPenalty;
        status_ = status;
    }

    // State queries
    bool isScheduled() const { return status_ == "scheduled"; }
    bool isInProgress() const { return status_ == "in_progress"; }
    bool isFinal() const { return status_ == "final"; }
    bool isPlayed() const { return isFinal() || isInProgress(); }

    bool isTie() const { return homeScore_ == awayScore_ && homeScore_ >= 0; }
    bool homeTeamWon() const {
        if (homeScore_ > awayScore_) return true;
        if (homeScore_ < awayScore_) return false;
        return homePenaltyScore_ > awayPenaltyScore_;
    }
    bool awayTeamWon() const {
        if (awayScore_ > homeScore_) return true;
        if (awayScore_ < homeScore_) return false;
        return awayPenaltyScore_ > homePenaltyScore_;
    }

private:
    int matchId_ = 0;
    std::string stage_;
    std::string group_;
    std::string date_;
    std::string homeTeam_;
    std::string awayTeam_;
    int homeScore_ = -1;
    int awayScore_ = -1;
    int homePenaltyScore_ = -1;
    int awayPenaltyScore_ = -1;
    std::string status_ = "scheduled"; // "scheduled", "in_progress", "final"
    std::string hostCity_;
};

#endif // MATCH_H
