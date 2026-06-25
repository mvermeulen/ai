#include "server.hpp"
#include <iostream>

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
