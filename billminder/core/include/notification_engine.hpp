#pragma once

#include "db.hpp"
#include "email_service.hpp"
#include <thread>
#include <atomic>
#include <memory>

namespace billminder {

class NotificationEngine {
public:
    NotificationEngine(std::shared_ptr<Database> db, std::shared_ptr<EmailService> email);
    ~NotificationEngine();

    void start();
    void stop();

private:
    void run();
    void check_and_send();

    std::shared_ptr<Database> db_;
    std::shared_ptr<EmailService> email_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace billminder
