#include "notification_engine.hpp"
#include "projection.hpp"
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>

namespace billminder {

NotificationEngine::NotificationEngine(std::shared_ptr<Database> db, std::shared_ptr<EmailService> email)
    : db_(std::move(db)), email_(std::move(email)) {}

NotificationEngine::~NotificationEngine() {
    stop();
}

void NotificationEngine::start() {
    if (!running_) {
        running_ = true;
        thread_ = std::thread(&NotificationEngine::run, this);
    }
}

void NotificationEngine::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void NotificationEngine::run() {
    while (running_) {
        check_and_send();
        
        // Sleep for a long time, but check running_ periodically
        // We sleep in 1-second chunks so we can be cleanly interrupted on stop()
        // Wait 7 days for weekly check
        for(int i = 0; i < 3600 * 24 * 7; ++i) { 
            if (!running_) break;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void NotificationEngine::check_and_send() {
    auto now = std::chrono::system_clock::now();
    auto today_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_today = *std::localtime(&today_t);
    char date_buf[20];
    std::strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", &tm_today);
    std::string today(date_buf);

    auto bills = db_->get_bills();
    // Project 14 days ahead so we catch things coming up soon
    auto projections = ProjectionEngine::generate_projections(bills, 14, today); 

    if (projections.empty()) return;

    std::stringstream body;
    body << "Hello from BillMinder!\r\n\r\n";
    body << "Here are your upcoming bills for the next 14 days:\r\n\r\n";

    double total = 0.0;
    for (const auto& p : projections) {
        body << "- " << p.name << ": $" << std::fixed << std::setprecision(2) << p.amount_expected 
             << " due on " << p.due_date << "\r\n";
        total += p.amount_expected;
    }

    body << "\r\nTotal expected: $" << std::fixed << std::setprecision(2) << total << "\r\n";
    body << "\r\nStay ahead of your finances!\r\n";

    if (email_) {
        std::cout << "Sending weekly email notification..." << std::endl;
        email_->send_email("Weekly BillMinder Report", body.str());
    }
}

} // namespace billminder
