#include "http_transport.h"

#include <cstdlib>
#include <vector>

#ifdef _WIN32
void close_http_socket(HttpSocket socket) {
    closesocket(socket);
}
#else
#include <sys/socket.h>
#include <unistd.h>

void close_http_socket(HttpSocket socket) {
    close(socket);
}
#endif

std::string receive_http_request(HttpSocket client) {
    std::string request;
    std::vector<char> buffer(4096);
    std::size_t expected_size = 0;

    for (;;) {
        const auto received = recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) break;
        request.append(buffer.data(), static_cast<std::size_t>(received));

        const auto headers_end = request.find("\r\n\r\n");
        if (headers_end != std::string::npos && expected_size == 0) {
            expected_size = headers_end + 4;
            const std::string content_length_header = "Content-Length:";
            const auto length_start = request.find(content_length_header);
            if (length_start != std::string::npos) {
                const auto value_start = length_start + content_length_header.size();
                expected_size += static_cast<std::size_t>(std::strtoul(request.c_str() + value_start, nullptr, 10));
            }
        }

        if (expected_size != 0 && request.size() >= expected_size) break;
        if (request.find("\r\n\r\n") != std::string::npos && expected_size == 0) break;
    }
    return request;
}
