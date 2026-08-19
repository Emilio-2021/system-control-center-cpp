#pragma once

#include <filesystem>
#include <string>

class HttpServer {
public:
    HttpServer(unsigned short port, std::filesystem::path web_root);

    int run();

private:
    std::string handle_request(const std::string& request) const;

    unsigned short port_;
    std::filesystem::path web_root_;
};
