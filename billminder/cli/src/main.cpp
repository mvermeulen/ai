#include <iostream>
#include <string>
#include <iomanip>
#include <httplib.h>
#include <json.hpp>
#include <chrono>

using json = nlohmann::json;

void print_usage(const char* prog) {
    std::cout << "BillMinder CLI\n";
    std::cout << "Usage: " << prog << " <command> [args...]\n\n";
    std::cout << "Commands:\n";
    std::cout << "  list                      List all active bills\n";
    std::cout << "  add <name> <amt> <date> <recur>  Add a new bill (e.g. add Internet 50.00 2026-07-01 monthly)\n";
    std::cout << "  pay <id> <amt>            Mark a bill as paid (e.g. pay bill-123 50.00)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    httplib::Client cli("http://localhost:8080");

    if (cmd == "list") {
        auto res = cli.Get("/api/bills");
        if (res && res->status == 200) {
            auto bills = json::parse(res->body);
            std::cout << std::left << std::setw(25) << "ID" 
                      << std::setw(20) << "Name" 
                      << std::setw(10) << "Amount" 
                      << std::setw(15) << "Due Date" 
                      << std::setw(12) << "Recurrence" 
                      << std::setw(10) << "Status" << "\n";
            std::cout << std::string(92, '-') << "\n";
            for (const auto& b : bills) {
                std::cout << std::left << std::setw(25) << b["id"].get<std::string>()
                          << std::setw(20) << b["name"].get<std::string>()
                          << "$" << std::setw(9) << std::fixed << std::setprecision(2) << b["amount_expected"].get<double>()
                          << std::setw(15) << b["due_date"].get<std::string>()
                          << std::setw(12) << b["recurrence_rule"].get<std::string>()
                          << std::setw(10) << b["status"].get<std::string>() << "\n";
            }
        } else {
            std::cerr << "Failed to list bills. Is the server running on port 8080?\n";
            if (res) std::cerr << "Status: " << res->status << "\n";
            return 1;
        }
    } else if (cmd == "add") {
        if (argc < 6) {
            std::cerr << "Missing arguments for 'add'\n";
            print_usage(argv[0]);
            return 1;
        }
        
        auto now = std::chrono::system_clock::now();
        auto today_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_today = *std::localtime(&today_t);
        char date_buf[20];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_today);
        
        json j = {
            {"id", "bill-" + std::to_string(today_t)},
            {"name", argv[2]},
            {"amount_expected", std::stod(argv[3])},
            {"due_date", argv[4]},
            {"recurrence_rule", argv[5]},
            {"payee", ""},
            {"status", "upcoming"},
            {"notes", ""},
            {"created_at", std::string(date_buf)},
            {"updated_at", std::string(date_buf)}
        };

        auto res = cli.Post("/api/bills", j.dump(), "application/json");
        if (res && res->status == 201) {
            std::cout << "Bill '" << argv[2] << "' added successfully.\n";
        } else {
            std::cerr << "Failed to add bill.\n";
            if (res) std::cerr << res->body << "\n";
            return 1;
        }
    } else if (cmd == "pay") {
        if (argc < 4) {
            std::cerr << "Missing arguments for 'pay'\n";
            print_usage(argv[0]);
            return 1;
        }

        auto now = std::chrono::system_clock::now();
        auto today_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm_today = *std::localtime(&today_t);
        char date_buf[20];
        std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_today);

        std::string id = argv[2];
        double amt = std::stod(argv[3]);
        
        json j = {
            {"amount_paid", amt},
            {"payment_date", std::string(date_buf)},
            {"notes", "Paid via CLI"}
        };
        
        auto res = cli.Post("/api/bills/" + id + "/pay", j.dump(), "application/json");
        if (res && res->status == 200) {
            std::cout << "Bill " << id << " marked as paid.\n";
        } else {
            std::cerr << "Failed to pay bill.\n";
            if (res) std::cerr << res->body << "\n";
            return 1;
        }
    } else {
        std::cerr << "Unknown command: " << cmd << "\n";
        print_usage(argv[0]);
        return 1;
    }

    return 0;
}
