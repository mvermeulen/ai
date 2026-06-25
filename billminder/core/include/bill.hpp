#pragma once
#include <string>
#include <json.hpp> // nlohmann/json

namespace billminder {

struct Bill {
    std::string id;
    std::string name;
    double amount_expected = 0.0;
    std::string due_date; // YYYY-MM-DD
    std::string recurrence_rule; // 'none', 'monthly', 'quarterly', 'yearly'
    std::string payee;
    std::string status; // 'upcoming', 'paid', 'overdue', 'skipped'
    std::string notes;
    std::string created_at;
    std::string updated_at;
};

// nlohmann::json serialization macros/hooks
inline void to_json(nlohmann::json& j, const Bill& b) {
    j = nlohmann::json{
        {"id", b.id},
        {"name", b.name},
        {"amount_expected", b.amount_expected},
        {"due_date", b.due_date},
        {"recurrence_rule", b.recurrence_rule},
        {"payee", b.payee},
        {"status", b.status},
        {"notes", b.notes},
        {"created_at", b.created_at},
        {"updated_at", b.updated_at}
    };
}

inline void from_json(const nlohmann::json& j, Bill& b) {
    if (j.contains("id")) j.at("id").get_to(b.id);
    j.at("name").get_to(b.name);
    j.at("amount_expected").get_to(b.amount_expected);
    j.at("due_date").get_to(b.due_date);
    j.at("recurrence_rule").get_to(b.recurrence_rule);
    if (j.contains("payee")) j.at("payee").get_to(b.payee);
    if (j.contains("status")) j.at("status").get_to(b.status);
    if (j.contains("notes")) j.at("notes").get_to(b.notes);
    if (j.contains("created_at")) j.at("created_at").get_to(b.created_at);
    if (j.contains("updated_at")) j.at("updated_at").get_to(b.updated_at);
}

} // namespace billminder
