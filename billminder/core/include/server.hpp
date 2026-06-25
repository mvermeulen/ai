#pragma once
#include <memory>
#include <httplib.h>
#include "db.hpp"

namespace billminder {

class Server {
public:
    explicit Server(std::shared_ptr<Database> db);
    void start(int port = 8080);
    void stop();

private:
    void setup_routes();
    std::shared_ptr<Database> db_;
    httplib::Server svr_;
};

} // namespace billminder
