#pragma once
#include <string>
#include <json.hpp> // nlohmann/json

namespace billminder {

struct Bill {
    std::string id;
    std::string name;
    double default_amount;
    std::string recurrence_rule;
    std::string payee;
    std::string url;
    std::string account;
    std::string password;
    std::string notes;
    std::string created_at;
    std::string updated_at;
};

struct BillInstance {
    std::string id;
    std::string bill_id;
    std::string bill_name; // Helpful for UI display
    std::string recurrence_rule; // Helpful for UI display
    double amount_expected;
    std::string due_date;
    std::string status;
    std::string notes;
    std::string created_at;
    std::string updated_at;
};

inline void to_json(nlohmann::json& j, const Bill& b) {
    j = nlohmann::json{
        {"id", b.id},
        {"name", b.name},
        {"default_amount", b.default_amount},
        {"recurrence_rule", b.recurrence_rule},
        {"payee", b.payee},
        {"url", b.url},
        {"account", b.account},
        {"password", b.password},
        {"notes", b.notes},
        {"created_at", b.created_at},
        {"updated_at", b.updated_at}
    };
}

inline void from_json(const nlohmann::json& j, Bill& b) {
    j.at("id").get_to(b.id);
    j.at("name").get_to(b.name);
    if (j.contains("default_amount")) j.at("default_amount").get_to(b.default_amount);
    if (j.contains("recurrence_rule")) j.at("recurrence_rule").get_to(b.recurrence_rule);
    if (j.contains("payee")) j.at("payee").get_to(b.payee);
    if (j.contains("url")) j.at("url").get_to(b.url);
    if (j.contains("account")) j.at("account").get_to(b.account);
    if (j.contains("password")) j.at("password").get_to(b.password);
    if (j.contains("notes")) j.at("notes").get_to(b.notes);
    if (j.contains("created_at")) j.at("created_at").get_to(b.created_at);
    if (j.contains("updated_at")) j.at("updated_at").get_to(b.updated_at);
}

inline void to_json(nlohmann::json& j, const BillInstance& b) {
    j = nlohmann::json{
        {"id", b.id},
        {"bill_id", b.bill_id},
        {"bill_name", b.bill_name},
        {"recurrence_rule", b.recurrence_rule},
        {"amount_expected", b.amount_expected},
        {"due_date", b.due_date},
        {"status", b.status},
        {"notes", b.notes},
        {"created_at", b.created_at},
        {"updated_at", b.updated_at}
    };
}

inline void from_json(const nlohmann::json& j, BillInstance& b) {
    j.at("id").get_to(b.id);
    j.at("bill_id").get_to(b.bill_id);
    if (j.contains("bill_name")) j.at("bill_name").get_to(b.bill_name);
    if (j.contains("recurrence_rule")) j.at("recurrence_rule").get_to(b.recurrence_rule);
    if (j.contains("amount_expected")) j.at("amount_expected").get_to(b.amount_expected);
    if (j.contains("due_date")) j.at("due_date").get_to(b.due_date);
    if (j.contains("status")) j.at("status").get_to(b.status);
    if (j.contains("notes")) j.at("notes").get_to(b.notes);
    if (j.contains("created_at")) j.at("created_at").get_to(b.created_at);
    if (j.contains("updated_at")) j.at("updated_at").get_to(b.updated_at);
}

} // namespace billminder
