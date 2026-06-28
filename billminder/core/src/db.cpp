#include "db.hpp"

namespace billminder {

Database::Database(const std::string& db_path) {
    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw DatabaseError("Failed to open database: " + err);
    }
}

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void Database::execute(const std::string& sql) {
    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "Unknown error";
        sqlite3_free(err_msg);
        throw DatabaseError("Failed to execute SQL: " + err);
    }
}

void Database::initialize_schema() {
    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS bills (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            amount_expected REAL NOT NULL,
            due_date TEXT NOT NULL,
            recurrence_rule TEXT NOT NULL, -- e.g. 'monthly', 'quarterly', 'yearly'
            payee TEXT,
            status TEXT NOT NULL,
            notes TEXT,
            group_id TEXT,
            next_instance_id TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS payment_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            bill_id TEXT NOT NULL,
            amount_paid REAL NOT NULL,
            payment_date TEXT NOT NULL,
            notes TEXT,
            FOREIGN KEY(bill_id) REFERENCES bills(id)
        );
    )";
    execute(schema);

    // Run migrations for existing database (ignore errors if columns exist)
    char* err_msg = nullptr;
    sqlite3_exec(db_, "ALTER TABLE bills ADD COLUMN group_id TEXT;", nullptr, nullptr, &err_msg);
    if(err_msg) sqlite3_free(err_msg);
    err_msg = nullptr;
    sqlite3_exec(db_, "ALTER TABLE bills ADD COLUMN next_instance_id TEXT;", nullptr, nullptr, &err_msg);
    if(err_msg) sqlite3_free(err_msg);
}

void Database::create_bill(const Bill& bill) {
    const char* sql = "INSERT INTO bills (id, name, amount_expected, due_date, recurrence_rule, payee, status, notes, group_id, next_instance_id, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, bill.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bill.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, bill.amount_expected);
    sqlite3_bind_text(stmt, 4, bill.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, bill.recurrence_rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, bill.payee.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, bill.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, bill.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, bill.group_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, bill.next_instance_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, bill.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, bill.updated_at.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to execute statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

std::vector<Bill> Database::get_bills() {
    const char* sql = "SELECT id, name, amount_expected, due_date, recurrence_rule, payee, status, notes, group_id, next_instance_id, created_at, updated_at FROM bills ORDER BY due_date ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }

    std::vector<Bill> bills;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Bill b;
        b.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        b.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        b.amount_expected = sqlite3_column_double(stmt, 2);
        b.due_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        b.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        b.payee = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        b.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        b.notes = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        b.group_id = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        b.next_instance_id = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        b.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        bills.push_back(b);
    }
    sqlite3_finalize(stmt);
    return bills;
}

Bill Database::get_bill(const std::string& id) {
    const char* sql = "SELECT id, name, amount_expected, due_date, recurrence_rule, payee, status, notes, group_id, next_instance_id, created_at, updated_at FROM bills WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Bill b;
        b.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        b.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        b.amount_expected = sqlite3_column_double(stmt, 2);
        b.due_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        b.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        b.payee = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        b.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        b.notes = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        b.group_id = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        b.next_instance_id = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        b.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        sqlite3_finalize(stmt);
        return b;
    }
    sqlite3_finalize(stmt);
    throw DatabaseError("Bill not found: " + id);
}

void Database::update_bill(const Bill& bill) {
    const char* sql = "UPDATE bills SET name=?, amount_expected=?, due_date=?, recurrence_rule=?, payee=?, status=?, notes=?, group_id=?, next_instance_id=?, updated_at=? WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }

    sqlite3_bind_text(stmt, 1, bill.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, bill.amount_expected);
    sqlite3_bind_text(stmt, 3, bill.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, bill.recurrence_rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, bill.payee.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, bill.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, bill.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, bill.group_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, bill.next_instance_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, bill.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, bill.id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to execute statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::delete_bill(const std::string& id) {
    const char* sql = "DELETE FROM bills WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to delete bill: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::pay_bill(const std::string& id, double amount_paid, const std::string& payment_date, const std::string& notes) {
    execute("BEGIN TRANSACTION;");
    try {
        const char* sql = "INSERT INTO payment_history (bill_id, amount_paid, payment_date, notes) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 2, amount_paid);
        sqlite3_bind_text(stmt, 3, payment_date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, notes.c_str(), -1, SQLITE_TRANSIENT);
        
        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            throw DatabaseError(std::string("Failed to record payment: ") + sqlite3_errmsg(db_));
        }
        sqlite3_finalize(stmt);

        // Update bill status to paid
        const char* upd = "UPDATE bills SET status='paid', updated_at=? WHERE id=?;";
        sqlite3_stmt* upd_stmt;
        if (sqlite3_prepare_v2(db_, upd, -1, &upd_stmt, nullptr) != SQLITE_OK) {
            throw DatabaseError(std::string("Failed to prepare update statement: ") + sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(upd_stmt, 1, payment_date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd_stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(upd_stmt) != SQLITE_DONE) {
            sqlite3_finalize(upd_stmt);
            throw DatabaseError(std::string("Failed to update bill status: ") + sqlite3_errmsg(db_));
        }
        sqlite3_finalize(upd_stmt);

        execute("COMMIT;");
    } catch (...) {
        execute("ROLLBACK;");
        throw;
    }
}

} // namespace billminder
