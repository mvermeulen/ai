#ifndef TEAM_H
#define TEAM_H

#include <string>

class Team {
public:
    Team() = default;
    Team(const std::string& abbreviation, const std::string& fullName,
         const std::string& group, double eloRating, const std::string& federation);

    // Accessors
    const std::string& abbreviation() const { return abbreviation_; }
    const std::string& getAbbreviation() const { return abbreviation_; }
    const std::string& fullName() const { return fullName_; }
    const std::string& group() const { return group_; }
    const std::string& getGroup() const { return group_; }
    double eloRating() const { return eloRating_; }
    double getEloRating() const { return eloRating_; }
    const std::string& federation() const { return federation_; }

    // Mutator
    void setEloRating(double elo) { eloRating_ = elo; }

    // Group stage stats accessors
    int wins() const { return wins_; }
    int draws() const { return draws_; }
    int losses() const { return losses_; }
    int gamesPlayed() const { return wins_ + draws_ + losses_; }
    int goalsFor() const { return goalsFor_; }
    int goalsAgainst() const { return goalsAgainst_; }
    int goalDifference() const { return goalsFor_ - goalsAgainst_; }
    int points() const { return wins_ * 3 + draws_ * 1; }

    // Stat mutators
    void addWin() { wins_++; }
    void addDraw() { draws_++; }
    void addLoss() { losses_++; }
    void addGoalsFor(int gf) { goalsFor_ += gf; }
    void addGoalsAgainst(int ga) { goalsAgainst_ += ga; }

    void resetRecord() {
        wins_ = 0;
        draws_ = 0;
        losses_ = 0;
        goalsFor_ = 0;
        goalsAgainst_ = 0;
    }

private:
    std::string abbreviation_;
    std::string fullName_;
    std::string group_;
    double eloRating_ = 1500.0;
    std::string federation_;

    int wins_ = 0;
    int draws_ = 0;
    int losses_ = 0;
    int goalsFor_ = 0;
    int goalsAgainst_ = 0;
};

#endif // TEAM_H
