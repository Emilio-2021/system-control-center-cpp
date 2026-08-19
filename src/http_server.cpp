#include "http_server.h"
#include "admin_routes.h"
#include "app_logger.h"
#include "auth_routes.h"
#include "dashboard_routes.h"
#include "http_request.h"
#include "http_response.h"
#include "http_transport.h"
#include "order_routes.h"
#include "product_routes.h"
#include "session_store.h"
#include "static_assets.h"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {

std::string decorate_response(std::string_view status, std::string_view type,
                              std::string_view body, std::string_view extra_headers = {}) {
    std::string rendered_body(body);
    const std::string plain_brand = R"(<a class="navbar-brand" href="/dashboard">System Control Center</a>)";
    const std::string arrow_brand = R"(<a class="navbar-brand" href="/dashboard">← System Control Center</a>)";
    std::string section_label = "System Control Center";
    if (rendered_body.find("Product Master Catalog") != std::string::npos) section_label = "Stock Room Registry";
    else if (rendered_body.find("Business Entities Registry") != std::string::npos) section_label = "Enterprise Business Entities Matrix";
    else if (rendered_body.find("Users and Roles") != std::string::npos) section_label = "User Records Management Grid";
    else if (rendered_body.find("Orders &amp; Invoiced Line Items") != std::string::npos) section_label = "Transaction Tracking Room";
    else if (rendered_body.find("Invoice Checkout Wizard") != std::string::npos || rendered_body.find("Create New Sales Order Invoice") != std::string::npos) section_label = "Sales Register Desk";
    else if (rendered_body.find("Order #") != std::string::npos) section_label = "Order Details";
    const std::string dashboard_brand = "<div class=\"d-flex align-items-center\"><a class=\"navbar-brand\" href=\"/dashboard\">← System Dashboard</a><span class=\"navbar-text text-white-50 ms-3\">" + section_label + "</span></div>";
    for (const auto& brand : {plain_brand, arrow_brand}) {
        std::size_t position = 0;
        while ((position = rendered_body.find(brand, position)) != std::string::npos) {
            rendered_body.replace(position, brand.size(), dashboard_brand);
            position += dashboard_brand.size();
        }
    }
    std::ostringstream output;
    output << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << extra_headers
           << "Content-Length: " << rendered_body.size() << "\r\n"
           << "Connection: close\r\n\r\n";
    output.write(rendered_body.data(), static_cast<std::streamsize>(rendered_body.size()));
    return output.str();
}

std::string handle_request(std::string_view request, const std::filesystem::path& root) {
    const auto path = request_path(request);
    if (path.empty()) return decorate_response("400 Bad Request", "text/plain; charset=utf-8", "Bad request\n");
    const auto method = request_method(request);
    if (path == "/health") return decorate_response("200 OK", "application/json; charset=utf-8", "{\"status\":\"ok\"}\n");
    if (method == "POST" && path == "/login") {
        const auto body_start = request.find("\r\n\r\n");
        return auth_login_result(root, body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4));
    }
    if (method == "GET" && path == "/") return auth_login_page(root, request);
    if (method == "GET" && path == "/products-view") return product_list_route(root, request);
    if (method == "POST" && (path == "/products/create" || path == "/products/update" || path.rfind("/products/delete/", 0) == 0)) return product_mutation_route(root, request, path);
    if (method == "GET" && path == "/entities-view") return entity_list_route(root, request);
    if (method == "POST" && (path == "/entities/create" || path == "/entities/update" || path.rfind("/entities/delete/", 0) == 0)) return entity_mutation_route(root, request, path);
    if (method == "GET" && path == "/users-view") return user_list_route(root, request);
    if (method == "POST" && (path == "/users/create" || path == "/users/update" || path.rfind("/users/delete/", 0) == 0)) return user_mutation_route(root, request, path);
    if (method == "GET" && path == "/checkout") return checkout_route(root, request);
    if (method == "POST" && path == "/checkout/create") return checkout_create_route(root, request);
    if (method == "GET" && path == "/orders-view") return orders_route(root, request);
    if (method == "GET" && path.rfind("/orders/", 0) == 0) return order_detail_route(root, request, static_cast<int>(std::strtol(std::string(path.substr(8)).c_str(), nullptr, 10)));
    if (method == "POST" && path.rfind("/orders/", 0) == 0 && path.find("/refund") != std::string::npos) return refund_route(root, request, static_cast<int>(std::strtol(std::string(path.substr(8, path.find("/refund") - 8)).c_str(), nullptr, 10)));
    if (method == "GET" && path == "/dashboard") return dashboard_route(root, request);
    if (method == "GET" && path == "/logout") {
        erase_session(request);
        return decorate_response("303 See Other", "text/plain; charset=utf-8", "", "Location: /\r\nSet-Cookie: scc_session=; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
    }
    if (path.find("..") != std::string::npos) return decorate_response("403 Forbidden", "text/plain; charset=utf-8", "Forbidden\n");
    const std::string relative = path == "/" ? "/templates/login.html" : std::string(path);
    if (relative.rfind("/static/", 0) != 0 && relative.rfind("/templates/", 0) != 0) return decorate_response("404 Not Found", "text/plain; charset=utf-8", "Not found\n");
    const auto file_path = root / relative.substr(1);
    const auto body = read_file(file_path);
    if (body.empty()) return decorate_response("404 Not Found", "text/plain; charset=utf-8", "Not found\n");
    return decorate_response("200 OK", content_type(file_path), body);
}

}

HttpServer::HttpServer(unsigned short port, std::filesystem::path web_root)
    : port_(port), web_root_(std::move(web_root)) {}

std::string HttpServer::handle_request(const std::string& request) const {
    const auto result = ::handle_request(request, web_root_);
    const auto status_end = result.find("\r\n");
    write_log(web_root_, "INFO", request_method(request) + " " + request_path(request) + " " + result.substr(0, status_end));
    return result;
}

int HttpServer::run() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) { std::cerr << "Unable to initialize Winsock\n"; return 1; }
#endif
    const HttpSocket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_http_socket) {
        std::cerr << "Unable to create server socket\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port_);
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || listen(server, 8) != 0) {
        std::cerr << "Unable to bind or listen on 127.0.0.1:" << port_ << "\n";
        close_http_socket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    std::cout << "System Control Center C++ web server\nListening at http://127.0.0.1:" << port_ << "\nPress Ctrl+C to stop.\n";
    write_log(web_root_, "INFO", "server_started port=" + std::to_string(port_));
    for (;;) {
        sockaddr_in client_address{};
#ifdef _WIN32
        int client_length = sizeof(client_address);
#else
        socklen_t client_length = sizeof(client_address);
#endif
        const HttpSocket client = accept(server, reinterpret_cast<sockaddr*>(&client_address), &client_length);
        if (client == invalid_http_socket) continue;
        const auto request = receive_http_request(client);
        if (!request.empty()) { const auto result = handle_request(request); send(client, result.data(), static_cast<int>(result.size()), 0); }
        close_http_socket(client);
    }
}
