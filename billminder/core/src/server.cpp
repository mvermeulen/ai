#include "server.hpp"
#include <iostream>
#include <chrono>
#include <cctype>
#include <map>
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

    auto instances = db_->get_instances();
    std::map<std::string, BillInstance> latest_instances;
    for (const auto& inst : instances) {
        if (latest_instances.find(inst.bill_id) == latest_instances.end()) {
            latest_instances[inst.bill_id] = inst;
        } else {
            if (inst.due_date > latest_instances[inst.bill_id].due_date) {
                latest_instances[inst.bill_id] = inst;
            }
        }
    }

    for (auto& [bill_id, inst] : latest_instances) {
        if (inst.recurrence_rule == "none") continue;
        if (inst.due_date >= today && inst.status != "paid") continue; 

        
        std::string next_date = ProjectionEngine::add_time_to_date(inst.due_date, inst.recurrence_rule, 1);
        
        if (inst.status == "upcoming") {
            inst.status = "overdue";
            inst.updated_at = today;
            db_->update_instance(inst);
        }

        std::string slug = inst.bill_name;
        for (char& c : slug) {
            if (c == ' ') c = '-';
            else c = std::tolower(c);
        }
        std::string new_id = slug + "-" + next_date;
        
        BillInstance next_inst;
        next_inst.id = new_id;
        next_inst.bill_id = inst.bill_id;
        next_inst.due_date = next_date;
        next_inst.amount_expected = inst.amount_expected;
        next_inst.status = "upcoming";
        next_inst.created_at = today;
        next_inst.updated_at = today;
        
        db_->create_instance(next_inst);
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

    // -----------------------------------------------------
    // Bill Template Endpoints
    // -----------------------------------------------------

    svr_.Get("/api/bills", [this, cors](const httplib::Request&, httplib::Response& res) {
        try {
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

    svr_.Post("/api/bills", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            auto j = json::parse(req.body);
            
            Bill b;
            b.id = j.at("id").get<std::string>();
            b.name = j.at("name").get<std::string>();
            b.default_amount = j.at("amount_expected").get<double>();
            b.recurrence_rule = j.at("recurrence_rule").get<std::string>();
            b.payee = j.value("payee", "");
            b.url = j.value("url", "");
            b.account = j.value("account", "");
            b.password = j.value("password", "");
            b.notes = j.value("notes", "");
            b.created_at = j.at("created_at").get<std::string>();
            b.updated_at = j.at("updated_at").get<std::string>();
            
            db_->create_bill(b);

            BillInstance inst;
            inst.id = b.id + "-" + j.at("due_date").get<std::string>();
            inst.bill_id = b.id;
            inst.amount_expected = b.default_amount;
            inst.due_date = j.at("due_date").get<std::string>();
            inst.status = "upcoming";
            inst.notes = b.notes;
            inst.created_at = b.created_at;
            inst.updated_at = b.updated_at;

            db_->create_instance(inst);

            cors(res);
            res.status = 201;
            res.set_content(json{{"status", "created"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

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

    svr_.Get(R"(/api/bills/([^/]+)/instances)", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto instances = db_->get_instances_for_bill(id);
            json j = instances;
            cors(res);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr_.Put(R"(/api/bills/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto j = json::parse(req.body);
            Bill b = j.get<Bill>();
            b.id = id;
            db_->update_bill(b);
            cors(res);
            res.set_content(json{{"status", "updated"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

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

    // -----------------------------------------------------
    // Bill Instance Endpoints
    // -----------------------------------------------------

    svr_.Get("/api/instances", [this, cors](const httplib::Request&, httplib::Response& res) {
        try {
            process_rollovers();
            auto instances = db_->get_instances();
            json j = instances;
            cors(res);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr_.Get(R"(/api/instances/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            BillInstance b = db_->get_instance(id);
            json j = b;
            cors(res);
            res.set_content(j.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 404;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr_.Put(R"(/api/instances/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto j = json::parse(req.body);
            BillInstance b;
            b.id = id;
            
            // Allow partial updates for the instance
            BillInstance existing = db_->get_instance(id);
            b.due_date = j.contains("due_date") ? j.at("due_date").get<std::string>() : existing.due_date;
            b.amount_expected = j.contains("amount_expected") ? j.at("amount_expected").get<double>() : existing.amount_expected;
            b.status = j.contains("status") ? j.at("status").get<std::string>() : existing.status;
            b.notes = j.contains("notes") ? j.at("notes").get<std::string>() : existing.notes;
            
            auto now = std::chrono::system_clock::now();
            auto today_t = std::chrono::system_clock::to_time_t(now);
            std::tm tm_today = *std::localtime(&today_t);
            char date_buf[20];
            std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_today);
            b.updated_at = std::string(date_buf);
            
            db_->update_instance(b);
            cors(res);
            res.set_content(json{{"status", "updated"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 400;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr_.Delete(R"(/api/instances/([^/]+))", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            db_->delete_instance(id);
            cors(res);
            res.set_content(json{{"status", "deleted"}}.dump(), "application/json");
        } catch (const std::exception& e) {
            cors(res);
            res.status = 500;
            res.set_content(json{{"error", e.what()}}.dump(), "application/json");
        }
    });

    svr_.Post(R"(/api/instances/([^/]+)/pay)", [this, cors](const httplib::Request& req, httplib::Response& res) {
        try {
            std::string id = req.matches[1];
            auto j = json::parse(req.body);
            double amount = j.at("amount_paid").get<double>();
            std::string date = j.at("payment_date").get<std::string>();
            std::string notes = j.value("notes", "");
            db_->pay_instance(id, amount, date, notes);
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
