#pragma once

#include <string>
#include <vector>

namespace billminder {

class EmailService {
public:
    EmailService(const std::string& smtp_url, const std::string& username, const std::string& password, const std::string& to_address);
    
    bool send_email(const std::string& subject, const std::string& body);

private:
    std::string smtp_url_;
    std::string username_;
    std::string password_;
    std::string to_address_;
};

} // namespace billminder
