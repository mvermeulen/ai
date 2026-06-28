#include "db.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstdlib>

namespace billminder {

namespace {
    const std::string SECRET_KEY = "billminder_local_secret_key_v1";

    std::string encrypt_password(const std::string& input) {
        if (input.empty()) return "";
        std::ostringstream oss;
        for (size_t i = 0; i < input.size(); ++i) {
            char enc = input[i] ^ SECRET_KEY[i % SECRET_KEY.size()];
            oss << std::hex << std::setw(2) << std::setfill('0') << (static_cast<int>(enc) & 0xFF);
        }
        return oss.str();
    }

    std::string decrypt_password(const std::string& input) {
        if (input.empty() || input.size() % 2 != 0) return "";
        std::string decrypted;
        for (size_t i = 0; i < input.size(); i += 2) {
            std::string byteString = input.substr(i, 2);
            char byte = static_cast<char>(std::strtol(byteString.c_str(), nullptr, 16));
            decrypted += byte ^ SECRET_KEY[(i / 2) % SECRET_KEY.size()];
        }
        return decrypted;
    }
}

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
    // Check if bills table exists and has the new columns
    bool needs_new_columns = false;
    sqlite3_stmt* check_stmt;
    if (sqlite3_prepare_v2(db_, "SELECT url FROM bills LIMIT 1;", -1, &check_stmt, nullptr) != SQLITE_OK) {
        // Table either doesn't exist, OR it exists but lacks 'url' (which means it also lacks account/password)
        // Let's check if 'id' exists to see if the table exists at all
        sqlite3_stmt* check_id_stmt;
        if (sqlite3_prepare_v2(db_, "SELECT id FROM bills LIMIT 1;", -1, &check_id_stmt, nullptr) == SQLITE_OK) {
            needs_new_columns = true;
            sqlite3_finalize(check_id_stmt);
        }
    }
    sqlite3_finalize(check_stmt);

    if (needs_new_columns) {
        std::cout << "Migrating database: Adding metadata columns to bills table..." << std::endl;
        char* err = nullptr;
        sqlite3_exec(db_, "ALTER TABLE bills ADD COLUMN url TEXT DEFAULT '';", nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(db_, "ALTER TABLE bills ADD COLUMN account TEXT DEFAULT '';", nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
        sqlite3_exec(db_, "ALTER TABLE bills ADD COLUMN password TEXT DEFAULT '';", nullptr, nullptr, &err);
        if (err) sqlite3_free(err);
    }

    const char* schema = R"(
        CREATE TABLE IF NOT EXISTS bills (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            default_amount REAL NOT NULL,
            recurrence_rule TEXT NOT NULL,
            payee TEXT,
            url TEXT DEFAULT '',
            account TEXT DEFAULT '',
            password TEXT DEFAULT '',
            notes TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );

        CREATE TABLE IF NOT EXISTS bill_instances (
            id TEXT PRIMARY KEY,
            bill_id TEXT NOT NULL,
            due_date TEXT NOT NULL,
            amount_expected REAL NOT NULL,
            status TEXT NOT NULL,
            notes TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            FOREIGN KEY(bill_id) REFERENCES bills(id)
        );

        CREATE TABLE IF NOT EXISTS payment_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            bill_instance_id TEXT NOT NULL,
            amount_paid REAL NOT NULL,
            payment_date TEXT NOT NULL,
            notes TEXT,
            FOREIGN KEY(bill_instance_id) REFERENCES bill_instances(id)
        );
    )";
    execute(schema);
}

// ---------------------------------------------------------
// Bill (Template) Operations
// ---------------------------------------------------------

void Database::create_bill(const Bill& bill) {
    const char* sql = "INSERT INTO bills (id, name, default_amount, recurrence_rule, payee, url, account, password, notes, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    
    std::string enc_pw = encrypt_password(bill.password);

    sqlite3_bind_text(stmt, 1, bill.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, bill.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, bill.default_amount);
    sqlite3_bind_text(stmt, 4, bill.recurrence_rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, bill.payee.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, bill.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, bill.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, enc_pw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, bill.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, bill.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, bill.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to execute statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

std::vector<Bill> Database::get_bills() {
    const char* sql = "SELECT id, name, default_amount, recurrence_rule, payee, url, account, password, notes, created_at, updated_at FROM bills ORDER BY name ASC;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    std::vector<Bill> bills;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Bill b;
        b.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        b.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        b.default_amount = sqlite3_column_double(stmt, 2);
        b.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        b.payee = sqlite3_column_type(stmt, 4) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        b.url = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        b.account = sqlite3_column_type(stmt, 6) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        std::string enc_pw = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        b.password = decrypt_password(enc_pw);
        
        b.notes = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        b.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        bills.push_back(b);
    }
    sqlite3_finalize(stmt);
    return bills;
}

Bill Database::get_bill(const std::string& id) {
    const char* sql = "SELECT id, name, default_amount, recurrence_rule, payee, url, account, password, notes, created_at, updated_at FROM bills WHERE id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Bill b;
        b.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        b.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        b.default_amount = sqlite3_column_double(stmt, 2);
        b.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        b.payee = sqlite3_column_type(stmt, 4) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        b.url = sqlite3_column_type(stmt, 5) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        b.account = sqlite3_column_type(stmt, 6) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        
        std::string enc_pw = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        b.password = decrypt_password(enc_pw);
        
        b.notes = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        b.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        b.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
        sqlite3_finalize(stmt);
        return b;
    }
    sqlite3_finalize(stmt);
    throw DatabaseError("Bill not found: " + id);
}

void Database::update_bill(const Bill& bill) {
    const char* sql = "UPDATE bills SET name=?, default_amount=?, recurrence_rule=?, payee=?, url=?, account=?, password=?, notes=?, updated_at=? WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    
    std::string enc_pw = encrypt_password(bill.password);
    
    sqlite3_bind_text(stmt, 1, bill.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, bill.default_amount);
    sqlite3_bind_text(stmt, 3, bill.recurrence_rule.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, bill.payee.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, bill.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, bill.account.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, enc_pw.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, bill.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, bill.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, bill.id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to update bill: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::delete_bill(const std::string& id) {
    execute("BEGIN TRANSACTION;");
    try {
        // Delete payment history for all instances of this bill
        const char* sql1 = "DELETE FROM payment_history WHERE bill_instance_id IN (SELECT id FROM bill_instances WHERE bill_id=?);";
        sqlite3_stmt* stmt1;
        if (sqlite3_prepare_v2(db_, sql1, -1, &stmt1, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt1, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt1);
            sqlite3_finalize(stmt1);
        }

        // Delete all instances
        const char* sql2 = "DELETE FROM bill_instances WHERE bill_id=?;";
        sqlite3_stmt* stmt2;
        if (sqlite3_prepare_v2(db_, sql2, -1, &stmt2, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt2, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt2);
            sqlite3_finalize(stmt2);
        }

        // Delete the bill
        const char* sql3 = "DELETE FROM bills WHERE id=?;";
        sqlite3_stmt* stmt3;
        if (sqlite3_prepare_v2(db_, sql3, -1, &stmt3, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt3, 1, id.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt3);
            sqlite3_finalize(stmt3);
        }

        execute("COMMIT;");
    } catch (...) {
        execute("ROLLBACK;");
        throw;
    }
}

// ---------------------------------------------------------
// Bill Instance Operations
// ---------------------------------------------------------

void Database::create_instance(const BillInstance& instance) {
    const char* sql = "INSERT INTO bill_instances (id, bill_id, due_date, amount_expected, status, notes, created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, instance.id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, instance.bill_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, instance.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, instance.amount_expected);
    sqlite3_bind_text(stmt, 5, instance.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, instance.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, instance.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, instance.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to create instance: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

std::vector<BillInstance> Database::get_instances() {
    const char* sql = R"(
        SELECT i.id, i.bill_id, b.name, b.recurrence_rule, i.amount_expected, i.due_date, i.status, i.notes, i.created_at, i.updated_at 
        FROM bill_instances i
        JOIN bills b ON i.bill_id = b.id
        ORDER BY i.due_date ASC;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    std::vector<BillInstance> instances;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BillInstance inst;
        inst.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        inst.bill_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        inst.bill_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        inst.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        inst.amount_expected = sqlite3_column_double(stmt, 4);
        inst.due_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        inst.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        inst.notes = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        inst.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        inst.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        instances.push_back(inst);
    }
    sqlite3_finalize(stmt);
    return instances;
}

std::vector<BillInstance> Database::get_instances_for_bill(const std::string& bill_id) {
    const char* sql = R"(
        SELECT i.id, i.bill_id, b.name, b.recurrence_rule, i.amount_expected, i.due_date, i.status, i.notes, i.created_at, i.updated_at 
        FROM bill_instances i
        JOIN bills b ON i.bill_id = b.id
        WHERE i.bill_id = ?
        ORDER BY i.due_date ASC;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, bill_id.c_str(), -1, SQLITE_TRANSIENT);
    
    std::vector<BillInstance> instances;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        BillInstance inst;
        inst.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        inst.bill_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        inst.bill_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        inst.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        inst.amount_expected = sqlite3_column_double(stmt, 4);
        inst.due_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        inst.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        inst.notes = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        inst.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        inst.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        instances.push_back(inst);
    }
    sqlite3_finalize(stmt);
    return instances;
}

BillInstance Database::get_instance(const std::string& id) {
    const char* sql = R"(
        SELECT i.id, i.bill_id, b.name, b.recurrence_rule, i.amount_expected, i.due_date, i.status, i.notes, i.created_at, i.updated_at 
        FROM bill_instances i
        JOIN bills b ON i.bill_id = b.id
        WHERE i.id = ?;
    )";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        BillInstance inst;
        inst.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        inst.bill_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        inst.bill_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        inst.recurrence_rule = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        inst.amount_expected = sqlite3_column_double(stmt, 4);
        inst.due_date = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        inst.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        inst.notes = sqlite3_column_type(stmt, 7) == SQLITE_NULL ? "" : reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        inst.created_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        inst.updated_at = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        sqlite3_finalize(stmt);
        return inst;
    }
    sqlite3_finalize(stmt);
    throw DatabaseError("Bill instance not found: " + id);
}

void Database::update_instance(const BillInstance& instance) {
    const char* sql = "UPDATE bill_instances SET due_date=?, amount_expected=?, status=?, notes=?, updated_at=? WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, instance.due_date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 2, instance.amount_expected);
    sqlite3_bind_text(stmt, 3, instance.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, instance.notes.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, instance.updated_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, instance.id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to update instance: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::delete_instance(const std::string& id) {
    const char* sql = "DELETE FROM bill_instances WHERE id=?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw DatabaseError(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        throw DatabaseError(std::string("Failed to delete instance: ") + sqlite3_errmsg(db_));
    }
    sqlite3_finalize(stmt);
}

void Database::pay_instance(const std::string& id, double amount_paid, const std::string& payment_date, const std::string& notes) {
    execute("BEGIN TRANSACTION;");
    try {
        const char* sql = "INSERT INTO payment_history (bill_instance_id, amount_paid, payment_date, notes) VALUES (?, ?, ?, ?);";
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

        // Update instance status to paid
        const char* upd = "UPDATE bill_instances SET status='paid', updated_at=? WHERE id=?;";
        sqlite3_stmt* upd_stmt;
        if (sqlite3_prepare_v2(db_, upd, -1, &upd_stmt, nullptr) != SQLITE_OK) {
            throw DatabaseError(std::string("Failed to prepare update statement: ") + sqlite3_errmsg(db_));
        }
        sqlite3_bind_text(upd_stmt, 1, payment_date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(upd_stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(upd_stmt) != SQLITE_DONE) {
            sqlite3_finalize(upd_stmt);
            throw DatabaseError(std::string("Failed to update instance status: ") + sqlite3_errmsg(db_));
        }
        sqlite3_finalize(upd_stmt);

        execute("COMMIT;");
    } catch (...) {
        execute("ROLLBACK;");
        throw;
    }
}

} // namespace billminder
