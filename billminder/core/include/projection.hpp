#pragma once
#include <vector>
#include <string>
#include "bill.hpp"

namespace billminder {

class ProjectionEngine {
public:
    // Generate projected bills for a given number of days into the future,
    // based on existing active bills and their recurrence rules.
    // 'current_date' is formatted as YYYY-MM-DD.
    static std::vector<Bill> generate_projections(const std::vector<Bill>& current_bills, int days_ahead, const std::string& current_date);

    // Helper to add periods (months, quarters, years) to an ISO string
    static std::string add_time_to_date(const std::string& date, const std::string& rule, int count);
};

} // namespace billminder
