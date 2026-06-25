#pragma once

#include <string>
#include <stdexcept>
#include <vector>
#include <sqlite3.h>
#include "bill.hpp"

namespace billminder {

class DatabaseError : public std::runtime_error {
public:
    explicit DatabaseError(const std::string& msg) : std::runtime_error(msg) {}
};

class Database {
public:
    explicit Database(const std::string& db_path);
    ~Database();

    // Prevent copy
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Run a statement that returns no data
    void execute(const std::string& sql);

    // Creates the initial schema if it doesn't exist
    void initialize_schema();

    // Bill CRUD operations
    void create_bill(const Bill& bill);
    std::vector<Bill> get_bills();
    Bill get_bill(const std::string& id);
    void update_bill(const Bill& bill);
    void delete_bill(const std::string& id);
    
    // Domain Operations
    void pay_bill(const std::string& id, double amount_paid, const std::string& payment_date, const std::string& notes = "");

private:
    sqlite3* db_ = nullptr;
};

} // namespace billminder
