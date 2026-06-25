#include <iostream>
#include <memory>
#include <fstream>
#include <string>
#include "db.hpp"
#include "server.hpp"
#include "notification_engine.hpp"
#include "email_service.hpp"

int main() {
    try {
        auto db = std::make_shared<billminder::Database>("data/bills.sqlite");
        db->initialize_schema();
        std::cout << "Database initialized." << std::endl;
        
        // Try to load SMTP config from .env (basic parsing)
        std::string smtp_url = "smtps://smtp.gmail.com:465";
        std::string smtp_user;
        std::string smtp_pass;
        std::string smtp_to;
        
        std::ifstream env_file(".env");
        if (env_file.is_open()) {
            std::string line;
            while (std::getline(env_file, line)) {
                if (line.find("SMTP_USER=") == 0) smtp_user = line.substr(10);
                if (line.find("SMTP_PASS=") == 0) smtp_pass = line.substr(10);
                if (line.find("SMTP_TO=") == 0) smtp_to = line.substr(8);
            }
        }

        std::shared_ptr<billminder::EmailService> email;
        if (!smtp_user.empty() && !smtp_pass.empty() && !smtp_to.empty()) {
            email = std::make_shared<billminder::EmailService>(smtp_url, smtp_user, smtp_pass, smtp_to);
            std::cout << "Email notification service configured." << std::endl;
        } else {
            std::cout << "Skipping email service (missing .env configuration)." << std::endl;
        }

        billminder::NotificationEngine notifier(db, email);
        notifier.start();

        billminder::Server server(db);
        server.start(8080);
        
        notifier.stop();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
