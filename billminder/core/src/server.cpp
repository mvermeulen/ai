#include "server.hpp"
#include <iostream>
#include <chrono>
#include <cctype>
#include "projection.hpp"

using json = nlohmann::json;

namespace billminder {

Server::Server(std::shared_ptr<Database> db) : db_(std::move(db)) {
    setup_routes();
}

void Server::start(int port) {
    std::cout << "Starting BillMinder server on port " << port << std::endl;
    svr_.listen("0.0.0.0", port);
}

void Server::stop() {
    svr_.stop();
}

void Server::process_rollovers() {
    auto now = std::chrono::system_clock::now();
    auto today_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_today = *std::localtime(&today_t);
    char date_buf[20];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_today);
    std::string today(date_buf);

    auto bills = db_->get_bills();
    for (auto& bill : bills) {
        if (bill.recurrence_rule == "none") continue;
        if (!bill.next_instance_id.empty()) continue; 
        if (bill.due_date >= today) continue; 
        
        std::string next_date = ProjectionEngine::add_time_to_date(bill.due_date, bill.recurrence_rule, 1);
        
        std::string slug = bill.group_id.empty() ? bill.name : bill.group_id;
        for (char& c : slug) {
            if (c == ' ') c = '-';
            else c = std::tolower(c);
        }
        std::string new_id = slug + "-" + next_date;
        
        Bill next_bill = bill;
        next_bill.id = new_id;
        next_bill.due_date = next_date;
        next_bill.status = "upcoming";
        next_bill.next_instance_id = "";
        next_bill.created_at = today;
        next_bill.updated_at = today;
        
        bill.next_instance_id = new_id;
        bill.updated_at = today;

        if (bill.status == "upcoming") {
            bill.status = "overdue";
        }
        
        db_->update_bill(bill);
        db_->create_bill(next_bill);
    }
}

void Server::setup_routes() {
    // Enable CORS for UI development
    svr_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    auto cors = [](httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
    };

    // GET /api/bills
    svr_.Get("/api/bills", [this, cors](const httplib::Request&, httplib::Response& res) {
        try {
            process_rollovers();
            auto bills = db_->get_bills();
            json j = bills;
            cors(res);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/bills
    svr_.Post("/api/bills", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            Bill b = j.get<Bill>();
            db_->create_bill(b);
            cors(res);
            res.status = 201;
            res.set_content(json{{"status", "created"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // GET /api/bills/:id
    svr_.Get(R"(/api/bills/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            Bill b = db_->get_bill(id);
            json j = b;
            cors(res);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 404;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // PUT /api/bills/:id
    svr_.Put(R"(/api/bills/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto j = json::parse(req.body);
            Bill b = j.get<Bill>();
            b.id = id; // Ensure ID matches URL
            db_->update_bill(b);
            cors(res);
            res.set_content(json{{"status", "updated"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // DELETE /api/bills/:id
    svr_.Delete(R"(/api/bills/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            db_->delete_bill(id);
            cors(res);
            res.set_content(json{{"status", "deleted"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    // POST /api/bills/:id/pay
    svr_.Post(R"(/api/bills/([^/]+)/pay)", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto j = json::parse(req.body);
            double amount = j.at("amount_paid").get<double>();
            std::string date = j.at("payment_date").get<std::string>();
            std::string notes = j.value("notes", "");
            db_->pay_bill(id, amount, date, notes);
            cors(res);
            res.set_content(json{{"status", "paid"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });
}

} // namespace billminder
