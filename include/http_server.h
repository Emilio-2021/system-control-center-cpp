#pragma once

#include <filesystem>
#include <string>

class HttpServer {
public:
    HttpServer(unsigned short port, std::filesystem::path web_root);

    // Handles one complete HTTP request. The network loop uses this method,
    // and keeping it public allows request-level integration tests without
    // binding a real socket.
    std::string handle_request(const std::string& request) const;

    int run();

private:
    unsigned short port_;
    std::filesystem::path web_root_;
};
