#include "auth_routes.h"

#include "http_request.h"
#include "http_response.h"
#include "scc_bcrypt.h"
#include "session_store.h"
#include "sqlite3.h"
#include <inja.hpp>
#include <nlohmann/json.hpp>

#include <iostream>

std::string auth_login_page(const std::filesystem::path& root, std::string_view request) {
    nlohmann::json data;
    data["error"] = query_value(request, "error");
    try {
        inja::Environment environment((root / "templates").string());
        environment.set_html_autoescape(true);
        const auto page = environment.render_file((root / "templates" / "login.html").string(), data);
        return response("200 OK", "text/html; charset=utf-8", page);
    } catch (const std::exception& error) {
        std::cerr << "Login template rendering failed: " << error.what() << "\n";
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Login template unavailable\n");
    }
}

std::string auth_login_result(const std::filesystem::path& root, std::string_view body) {
    const auto username = form_value(body, "username");
    const auto password = form_value(body, "password");
    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }
    sqlite3_stmt* statement = nullptr;
    const auto prepared = sqlite3_prepare_v2(database, "SELECT password_hash, role FROM users WHERE username = ?1", -1, &statement, nullptr);
    if (prepared != SQLITE_OK) {
        sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Authentication unavailable\n");
    }
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::string password_hash;
    if (sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_text(statement, 0) != nullptr) {
        password_hash = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    if (password_hash.empty() || !bcrypt::validatePassword(password, password_hash)) {
        return redirect("/?error=Invalid%20username%20or%20password");
    }
    const auto session_token = create_session(username);
    return response("303 See Other", "text/plain; charset=utf-8", "",
                    "Location: /dashboard\r\nSet-Cookie: scc_session=" + session_token + "; HttpOnly; SameSite=Lax\r\n");
}
