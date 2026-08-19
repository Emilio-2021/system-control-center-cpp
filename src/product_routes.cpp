#include "product_routes.h"

#include "http_request.h"
#include "http_response.h"
#include "session_store.h"
#include "sqlite3.h"
#include <inja.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
bool is_admin(const std::filesystem::path& root, std::string_view request) {
    const auto username = session_username(request);
    if (username.empty()) return false;
    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return false;
    }
    sqlite3_stmt* statement = nullptr;
    bool admin = false;
    if (sqlite3_prepare_v2(database, "SELECT role FROM users WHERE username = ?1", -1, &statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0) != nullptr) {
            admin = std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "admin";
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return admin;
}
}

std::string product_list_route(const std::filesystem::path& root, std::string_view request) {
    const auto username = session_username(request);
    if (username.empty()) return redirect("/?error=Please%20sign%20in");
    const std::vector<std::string> columns = {"id", "name", "sku", "price", "stock_quantity", "created_at"};
    auto sort_by = query_value(request, "sort_by");
    auto order = query_value(request, "order");
    if (std::find(columns.begin(), columns.end(), sort_by) == columns.end()) sort_by = "id";
    if (order != "DESC") order = "ASC";
    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }
    std::string role = "viewer";
    sqlite3_stmt* role_statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT role FROM users WHERE username = ?1", -1, &role_statement, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(role_statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(role_statement) == SQLITE_ROW && sqlite3_column_text(role_statement, 0) != nullptr) role = reinterpret_cast<const char*>(sqlite3_column_text(role_statement, 0));
    }
    sqlite3_finalize(role_statement);
    const auto sql = "SELECT id, name, sku, price, stock_quantity, created_at FROM products ORDER BY " + sort_by + " " + order;
    sqlite3_stmt* statement = nullptr;
    nlohmann::json data;
    data["username"] = username; data["role"] = role; data["current_sort"] = sort_by; data["current_order"] = order; data["next_order"] = order == "ASC" ? "DESC" : "ASC"; data["product_count"] = 0;
    for (const auto& column : columns) data["columns"].push_back(column);
    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            nlohmann::json row;
            row["id"] = sqlite3_column_int(statement, 0); row["name"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1)); row["sku"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2)); row["price"] = sqlite3_column_double(statement, 3); row["stock_quantity"] = sqlite3_column_int(statement, 4); row["created_at"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
            data["rows"].push_back(row); data["product_count"] = data["product_count"].get<int>() + 1;
        }
    }
    sqlite3_finalize(statement); sqlite3_close(database);
    try { inja::Environment environment((root / "templates").string()); environment.set_html_autoescape(true); return response("200 OK", "text/html; charset=utf-8", environment.render_file((root / "templates" / "products.html").string(), data)); }
    catch (const std::exception& error) { std::cerr << "Products template rendering failed: " << error.what() << "\n"; return response("500 Internal Server Error", "text/plain; charset=utf-8", std::string("Products template unavailable: ") + error.what() + "\n"); }
}

std::string product_mutation_route(const std::filesystem::path& root, std::string_view request, std::string_view path) {
    if (session_username(request).empty()) return redirect("/?error=Please%20sign%20in");
    if (!is_admin(root, request)) return redirect("/products-view?error=Administrator%20access%20required");
    sqlite3* database = nullptr; const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) { if (database != nullptr) sqlite3_close(database); return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n"); }
    const auto body_start = request.find("\r\n\r\n"); const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4); sqlite3_stmt* statement = nullptr; int result = SQLITE_ERROR;
    if (path == "/products/create") { result = sqlite3_prepare_v2(database, "INSERT INTO products (name, sku, price, stock_quantity) VALUES (?1, ?2, ?3, ?4)", -1, &statement, nullptr); if (result == SQLITE_OK) { sqlite3_bind_text(statement, 1, form_value(body, "name").c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 2, form_value(body, "sku").c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 3, std::strtod(form_value(body, "price").c_str(), nullptr)); sqlite3_bind_int(statement, 4, static_cast<int>(std::strtol(form_value(body, "stock_quantity").c_str(), nullptr, 10))); } }
    else if (path == "/products/update") { result = sqlite3_prepare_v2(database, "UPDATE products SET name = ?1, sku = ?2, price = ?3, stock_quantity = ?4 WHERE id = ?5", -1, &statement, nullptr); if (result == SQLITE_OK) { sqlite3_bind_text(statement, 1, form_value(body, "name").c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_text(statement, 2, form_value(body, "sku").c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_double(statement, 3, std::strtod(form_value(body, "price").c_str(), nullptr)); sqlite3_bind_int(statement, 4, static_cast<int>(std::strtol(form_value(body, "stock_quantity").c_str(), nullptr, 10))); sqlite3_bind_int(statement, 5, static_cast<int>(std::strtol(form_value(body, "id").c_str(), nullptr, 10))); } }
    else if (path.rfind("/products/delete/", 0) == 0) { result = sqlite3_prepare_v2(database, "DELETE FROM products WHERE id = ?1", -1, &statement, nullptr); if (result == SQLITE_OK) sqlite3_bind_int(statement, 1, static_cast<int>(std::strtol(std::string(path.substr(std::string("/products/delete/").size())).c_str(), nullptr, 10))); }
    if (result == SQLITE_OK) { sqlite3_exec(database, "BEGIN", nullptr, nullptr, nullptr); result = sqlite3_step(statement); if (result == SQLITE_DONE) sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr); else sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr); }
    sqlite3_finalize(statement); sqlite3_close(database); if (result != SQLITE_DONE) return redirect("/products-view?error=Product%20operation%20failed"); return redirect("/products-view");
}
