#include "http_server.h"

#include <cstdlib>
#include <filesystem>

namespace {

std::filesystem::path find_web_root(const char* executable_path) {
    const auto working_directory = std::filesystem::current_path();
    if (std::filesystem::exists(working_directory / "templates")) {
        return working_directory;
    }

    if (executable_path != nullptr) {
        const auto executable_directory =
            std::filesystem::absolute(executable_path).parent_path();
        if (std::filesystem::exists(executable_directory / "templates")) {
            return executable_directory;
        }

        const auto project_directory = executable_directory.parent_path();
        if (std::filesystem::exists(project_directory / "templates")) {
            return project_directory;
        }
    }

    return working_directory;
}

} // namespace

int main(int argc, char* argv[]) {
    unsigned short port = 8080;
    if (argc > 1) {
        const auto requested_port = std::strtol(argv[1], nullptr, 10);
        if (requested_port < 1 || requested_port > 65535) {
            return 1;
        }
        port = static_cast<unsigned short>(requested_port);
    }

    return HttpServer(port, find_web_root(argc > 0 ? argv[0] : nullptr)).run();
}
