#include "Team.h"

Team::Team(const std::string& abbreviation, const std::string& fullName,
           const std::string& group, double eloRating, const std::string& federation)
    : abbreviation_(abbreviation), fullName_(fullName),
      eloRating_(eloRating), federation_(federation) {
    if (group.size() > 6 && group.substr(0, 6) == "Group ") {
        group_ = group.substr(6);
    } else {
        group_ = group;
    }
}
