#include "dashboard_routes.h"

#include "http_response.h"
#include "session_store.h"
#include "sqlite3.h"
#include <inja.hpp>
#include <nlohmann/json.hpp>

#include <iostream>
#include <utility>
#include <vector>

std::string dashboard_route(const std::filesystem::path& root, std::string_view request) {
    const auto username = session_username(request);
    if (username.empty()) return redirect("/?error=Please%20sign%20in");

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
        if (sqlite3_step(role_statement) == SQLITE_ROW && sqlite3_column_text(role_statement, 0) != nullptr) {
            role = reinterpret_cast<const char*>(sqlite3_column_text(role_statement, 0));
        }
    }
    sqlite3_finalize(role_statement);

    std::vector<std::pair<std::string, int>> entity_breakdown;
    sqlite3_stmt* entity_statement = nullptr;
    const char* entity_sql = "SELECT et.entity, COUNT(*) FROM entities e INNER JOIN entity_type et ON e.entity_type = et.id GROUP BY et.id, et.entity ORDER BY et.entity";
    if (sqlite3_prepare_v2(database, entity_sql, -1, &entity_statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(entity_statement) == SQLITE_ROW) {
            entity_breakdown.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(entity_statement, 0)), sqlite3_column_int(entity_statement, 1));
        }
    }
    sqlite3_finalize(entity_statement);

    struct RecentOrder { int id; std::string customer; std::string status; double total; };
    std::vector<RecentOrder> recent_orders;
    sqlite3_stmt* order_statement = nullptr;
    const char* order_sql = "SELECT o.id, e.name, o.status, COALESCE(SUM(oi.quantity * oi.unit_price), 0) FROM orders o INNER JOIN entities e ON o.entity_id = e.id LEFT JOIN order_items oi ON oi.order_id = o.id GROUP BY o.id, e.name, o.status, o.created_at ORDER BY o.created_at DESC LIMIT 5";
    if (sqlite3_prepare_v2(database, order_sql, -1, &order_statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(order_statement) == SQLITE_ROW) {
            recent_orders.push_back({sqlite3_column_int(order_statement, 0), reinterpret_cast<const char*>(sqlite3_column_text(order_statement, 1)), reinterpret_cast<const char*>(sqlite3_column_text(order_statement, 2)), sqlite3_column_double(order_statement, 3)});
        }
    }
    sqlite3_finalize(order_statement);
    sqlite3_close(database);

    nlohmann::json data;
    data["username"] = username;
    data["role"] = role;
    for (const auto& item : entity_breakdown) data["entity_breakdown"].push_back({{"entity_type", item.first}, {"qty", item.second}});
    for (const auto& order : recent_orders) data["recent_orders"].push_back({{"order_id", order.id}, {"customer_name", order.customer}, {"status", order.status}, {"order_total", order.total}});

    try {
        inja::Environment environment((root / "templates").string());
        environment.set_html_autoescape(true);
        return response("200 OK", "text/html; charset=utf-8", environment.render_file((root / "templates" / "dashboard.html").string(), data));
    } catch (const std::exception& error) {
        std::cerr << "Template rendering failed: " << error.what() << "\n";
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Dashboard template unavailable\n");
    }
}
