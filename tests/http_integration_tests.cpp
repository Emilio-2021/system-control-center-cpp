#include "http_server.h"
#include "sqlite3.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string request(const std::string& method, const std::string& target,
                    const std::string& body = {}, const std::string& cookie = {}) {
    std::string result = method + " " + target + " HTTP/1.1\r\nHost: test\r\n";
    if (!cookie.empty()) result += "Cookie: " + cookie + "\r\n";
    if (!body.empty()) {
        result += "Content-Type: application/x-www-form-urlencoded\r\n";
        result += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    return result + "\r\n" + body;
}

std::string header_value(const std::string& response, const std::string& name) {
    const auto start = response.find(name);
    if (start == std::string::npos) return {};
    const auto value_start = start + name.size();
    const auto value_end = response.find("\r\n", value_start);
    return response.substr(value_start, value_end - value_start);
}

std::filesystem::path make_fixture() {
    const auto source = std::filesystem::path(SCC_SOURCE_DIR);
    const auto suffix = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto root = std::filesystem::temp_directory_path() / ("scc_http_test_" + suffix);
    std::filesystem::create_directories(root / "data");
    std::filesystem::copy(source / "templates", root / "templates",
                          std::filesystem::copy_options::recursive);
    std::filesystem::copy_file(source / "data" / "system_control_center.db",
                                root / "data" / "system_control_center.db");
    return root;
}

int find_id(const std::filesystem::path& root, const char* table,
            const char* column, const std::string& value) {
    sqlite3* database = nullptr;
    const auto file = (root / "data" / "system_control_center.db").string();
    assert(sqlite3_open_v2(file.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    const std::string sql = std::string("SELECT id FROM ") + table + " WHERE " + column + " = ?1";
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK);
    sqlite3_bind_text(statement, 1, value.c_str(), -1, SQLITE_TRANSIENT);
    const int id = sqlite3_step(statement) == SQLITE_ROW ? sqlite3_column_int(statement, 0) : 0;
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return id;
}

} // namespace

int main() {
    const auto root = make_fixture();
    const auto cleanup = [&] { std::filesystem::remove_all(root); };

    HttpServer server(0, root);

    const auto health = server.handle_request(request("GET", "/health"));
    assert(health.find("200 OK") != std::string::npos);
    assert(health.find("\"status\":\"ok\"") != std::string::npos);

    const auto protected_page = server.handle_request(request("GET", "/dashboard"));
    assert(protected_page.find("303 See Other") != std::string::npos);
    assert(protected_page.find("Location: /?error=Please%20sign%20in") != std::string::npos);

    const auto login = server.handle_request(request(
        "POST", "/login", "username=admin&password=admin123"));
    assert(login.find("303 See Other") != std::string::npos);
    assert(login.find("Location: /dashboard") != std::string::npos);
    const auto cookie = header_value(login, "Set-Cookie: ");
    assert(cookie.rfind("scc_session=", 0) == 0);
    const auto session_cookie = cookie.substr(0, cookie.find(';'));

    const auto dashboard = server.handle_request(request("GET", "/dashboard", {}, session_cookie));
    assert(dashboard.find("200 OK") != std::string::npos);
    assert(dashboard.find("admin") != std::string::npos);

    const auto users = server.handle_request(request("GET", "/users-view", {}, session_cookie));
    assert(users.find("200 OK") != std::string::npos);
    assert(users.find("Users and Roles") != std::string::npos);
    assert(users.find("← System Dashboard") != std::string::npos);
    assert(users.find("User Records Management Grid") != std::string::npos);

    const auto checkout = server.handle_request(request("GET", "/checkout", {}, session_cookie));
    assert(checkout.find("200 OK") != std::string::npos);
    assert(checkout.find("Create New Sales Order") != std::string::npos);
    assert(checkout.find("Discard invoice") != std::string::npos);

    const auto product_create = server.handle_request(request(
        "POST", "/products/create",
        "name=Integration%20Product&sku=INT-001&price=12.50&stock_quantity=9", session_cookie));
    assert(product_create.find("Location: /products-view") != std::string::npos);
    assert(server.handle_request(request("GET", "/products-view", {}, session_cookie)).find("Integration Product") != std::string::npos);
    const auto products_page = server.handle_request(request("GET", "/products-view", {}, session_cookie));
    assert(products_page.find("Stock Room Registry") != std::string::npos);
    const auto sorted_products = server.handle_request(request("GET", "/products-view?sort_by=name&order=DESC", {}, session_cookie));
    assert(sorted_products.find("sort_by=name&amp;order=ASC") != std::string::npos);
    assert(sorted_products.find("▼") != std::string::npos);
    const int product_id = find_id(root, "products", "sku", "INT-001");
    assert(product_id > 0);
    const auto product_update = server.handle_request(request(
        "POST", "/products/update",
        "id=" + std::to_string(product_id) + "&name=Updated%20Product&sku=INT-001&price=15.00&stock_quantity=11", session_cookie));
    assert(product_update.find("Location: /products-view") != std::string::npos);

    const auto entity_create = server.handle_request(request(
        "POST", "/entities/create",
        "entity_type=PERSON&name=Integration%20Customer&email=integration%40example.com", session_cookie));
    assert(entity_create.find("Location: /entities-view") != std::string::npos);
    assert(server.handle_request(request("GET", "/entities-view", {}, session_cookie)).find("Integration Customer") != std::string::npos);
    const int entity_id = find_id(root, "entities", "name", "Integration Customer");
    assert(entity_id > 0);
    const auto entity_update = server.handle_request(request(
        "POST", "/entities/update",
        "id=" + std::to_string(entity_id) + "&entity_type=COMPANY&name=Updated%20Customer&email=updated%40example.com", session_cookie));
    assert(entity_update.find("Location: /entities-view") != std::string::npos);

    const auto order_create = server.handle_request(request(
        "POST", "/checkout/create",
        "entity_id=" + std::to_string(entity_id) + "&product_id=" + std::to_string(product_id) + "&quantity=2", session_cookie));
    const auto order_location = header_value(order_create, "Location: ");
    assert(order_location.rfind("/orders/", 0) == 0);
    const int order_id = std::stoi(order_location.substr(std::string("/orders/").size()));
    const auto order_detail = server.handle_request(request(
        "GET", "/orders/" + std::to_string(order_id), {}, session_cookie));
    assert(order_detail.find("Order Summary") != std::string::npos);
    assert(order_detail.find("Invoice Line Items") != std::string::npos);
    const auto dashboard_order_detail = server.handle_request(request(
        "GET", "/orders/" + std::to_string(order_id) + "?back=dashboard", {}, session_cookie));
    assert(dashboard_order_detail.find("← System Dashboard") != std::string::npos);

    const auto user_create = server.handle_request(request(
        "POST", "/users/create",
        "username=integration_user&email=integration%40example.com&role=viewer&password=integration123", session_cookie));
    assert(user_create.find("Location: /users-view") != std::string::npos);
    assert(server.handle_request(request("GET", "/users-view", {}, session_cookie)).find("integration_user") != std::string::npos);
    const int user_id = find_id(root, "users", "username", "integration_user");
    assert(user_id > 0);
    const auto user_update = server.handle_request(request(
        "POST", "/users/update",
        "id=" + std::to_string(user_id) + "&username=integration_user&email=updated%40example.com&role=operator", session_cookie));
    assert(user_update.find("Location: /users-view") != std::string::npos);

    const auto logout = server.handle_request(request("GET", "/logout", {}, session_cookie));
    assert(logout.find("303 See Other") != std::string::npos);
    const auto after_logout = server.handle_request(request("GET", "/dashboard", {}, session_cookie));
    assert(after_logout.find("Location: /?error=Please%20sign%20in") != std::string::npos);

    cleanup();
    return 0;
}
