#include "projection.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace billminder {

static std::tm parse_date(const std::string& date) {
    std::tm tm = {};
    std::istringstream ss(date);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    if (ss.fail()) {
        throw std::invalid_argument("Failed to parse date: " + date);
    }
    // mktime needs daylight savings flag set or left negative, otherwise it can drift
    tm.tm_isdst = -1;
    return tm;
}

static std::string format_date(const std::tm& tm) {
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d");
    return ss.str();
}

static int days_difference(const std::string& start, const std::string& end) {
    std::tm tm_start = parse_date(start);
    std::tm tm_end = parse_date(end);
    std::time_t time_start = std::mktime(&tm_start);
    std::time_t time_end = std::mktime(&tm_end);
    double diff = std::difftime(time_end, time_start);
    return static_cast<int>(diff / (60 * 60 * 24));
}

std::string ProjectionEngine::add_time_to_date(const std::string& date, const std::string& rule, int count) {
    std::tm tm = parse_date(date);
    if (rule == "monthly") {
        tm.tm_mon += count;
    } else if (rule == "quarterly") {
        tm.tm_mon += (count * 3);
    } else if (rule == "yearly") {
        tm.tm_year += count;
    }
    // mktime normalizes out-of-range fields (e.g. month > 11)
    std::mktime(&tm);
    return format_date(tm);
}

std::vector<BillInstance> ProjectionEngine::generate_projections(const std::vector<BillInstance>& current_bills, int days_ahead, const std::string& current_date) {
    std::vector<BillInstance> projections;

    for (const auto& bill : current_bills) {
        // Skip non-recurring bills or bills that have a terminal status
        if (bill.recurrence_rule == "none") {
            continue;
        }

        int count = 1;
        while (true) {
            std::string next_date = add_time_to_date(bill.due_date, bill.recurrence_rule, count);
            int next_diff = days_difference(current_date, next_date);
            
            // Stop projecting if we exceed the time horizon
            if (next_diff > days_ahead) {
                break;
            }
            
            // Only add if it's strictly in the future relative to the current date window 
            // (or today if next_diff == 0)
            if (next_diff >= 0) {
                BillInstance projected_bill = bill;
                projected_bill.id = bill.id + "-proj-" + std::to_string(count);
                projected_bill.due_date = next_date;
                projected_bill.status = "projected";
                projections.push_back(projected_bill);
            }
            count++;
        }
    }

    return projections;
}

} // namespace billminder
