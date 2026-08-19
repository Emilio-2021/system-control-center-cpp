#include "http_server.h"

#include "scc_bcrypt.h"
#include "sqlite3.h"
#include <inja.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

std::mutex sessions_mutex;
std::unordered_map<std::string, std::string> sessions;

#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket socket) { closesocket(socket); }
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket socket) { close(socket); }
#endif

std::string content_type(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "application/javascript; charset=utf-8";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    return "application/octet-stream";
}

std::string response(std::string_view status, std::string_view type,
                     std::string_view body, std::string_view extra_headers = {}) {
    std::ostringstream output;
    output << "HTTP/1.1 " << status << "\r\n"
           << "Content-Type: " << type << "\r\n"
           << extra_headers
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << "\r\n";
    output.write(body.data(), static_cast<std::streamsize>(body.size()));
    return output.str();
}

std::string create_session(const std::string& username) {
    std::random_device random;
    std::mt19937_64 generator(random());
    std::ostringstream token;
    token << std::hex << generator() << generator();
    const auto session_token = token.str();
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions[session_token] = username;
    return session_token;
}

std::string session_token(std::string_view request) {
    const auto cookie_start = request.find("Cookie:");
    if (cookie_start == std::string_view::npos) return {};
    const auto line_end = request.find("\r\n", cookie_start);
    const auto cookie_line = request.substr(cookie_start, line_end - cookie_start);
    const std::string name = "scc_session=";
    const auto token_start = cookie_line.find(name);
    if (token_start == std::string_view::npos) return {};
    const auto value_start = token_start + name.size();
    const auto value_end = cookie_line.find(';', value_start);
    return std::string(cookie_line.substr(value_start, value_end == std::string_view::npos ? cookie_line.size() - value_start : value_end - value_start));
}

std::string session_username(std::string_view request) {
    const auto token = session_token(request);
    if (token.empty()) return {};
    std::lock_guard<std::mutex> lock(sessions_mutex);
    const auto session = sessions.find(token);
    return session == sessions.end() ? std::string{} : session->second;
}

std::string redirect(std::string_view location) {
    std::ostringstream output;
    output << "HTTP/1.1 303 See Other\r\n"
           << "Location: " << location << "\r\n"
           << "Content-Length: 0\r\n"
           << "Connection: close\r\n\r\n";
    return output.str();
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

std::string request_path(std::string_view request) {
    const auto first_line_end = request.find("\r\n");
    const auto first_line = request.substr(0, first_line_end);
    const auto first_space = first_line.find(' ');
    const auto second_space = first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) {
        return {};
    }
    auto path = std::string(first_line.substr(first_space + 1, second_space - first_space - 1));
    const auto query_start = path.find('?');
    if (query_start != std::string::npos) path.resize(query_start);
    return path;
}

std::string request_method(std::string_view request) {
    const auto first_space = request.find(' ');
    if (first_space == std::string_view::npos) return {};
    return std::string(request.substr(0, first_space));
}

std::string url_decode(std::string_view value) {
    std::string decoded;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const auto code = std::strtol(std::string(hex).c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                decoded.push_back(static_cast<char>(code));
                i += 2;
            } else {
                decoded.push_back(value[i]);
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

std::string form_value(std::string_view body, std::string_view key) {
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string_view::npos ? body.size() - start : end - start);
        const auto equals = part.find('=');
        if (equals != std::string_view::npos && part.substr(0, equals) == key) {
            return url_decode(part.substr(equals + 1));
        }
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return {};
}

std::vector<std::string> form_values(std::string_view body, std::string_view key) {
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string_view::npos ? body.size() - start : end - start);
        const auto equals = part.find('=');
        if (equals != std::string_view::npos && part.substr(0, equals) == key) values.push_back(url_decode(part.substr(equals + 1)));
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return values;
}

std::string query_value(std::string_view request, std::string_view key) {
    const auto first_line_end = request.find("\r\n");
    const auto first_line = request.substr(0, first_line_end);
    const auto first_space = first_line.find(' ');
    const auto second_space = first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) return {};
    const auto target = first_line.substr(first_space + 1, second_space - first_space - 1);
    const auto query_start = target.find('?');
    if (query_start == std::string_view::npos) return {};
    return form_value(target.substr(query_start + 1), key);
}

std::string login_page(const std::filesystem::path& root, std::string_view request) {
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

std::string products_result(const std::filesystem::path& root, std::string_view request) {
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
        if (sqlite3_step(role_statement) == SQLITE_ROW && sqlite3_column_text(role_statement, 0) != nullptr) {
            role = reinterpret_cast<const char*>(sqlite3_column_text(role_statement, 0));
        }
    }
    sqlite3_finalize(role_statement);

    const auto sql = "SELECT id, name, sku, price, stock_quantity, created_at FROM products ORDER BY " + sort_by + " " + order;
    sqlite3_stmt* statement = nullptr;
    nlohmann::json data;
    data["username"] = username;
    data["role"] = role;
    data["current_sort"] = sort_by;
    data["current_order"] = order;
    data["next_order"] = order == "ASC" ? "DESC" : "ASC";
    data["product_count"] = 0;
    for (const auto& column : columns) data["columns"].push_back(column);

    if (sqlite3_prepare_v2(database, sql.c_str(), -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            nlohmann::json row;
            row["id"] = sqlite3_column_int(statement, 0);
            row["name"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            row["sku"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            row["price"] = sqlite3_column_double(statement, 3);
            row["stock_quantity"] = sqlite3_column_int(statement, 4);
            row["created_at"] = reinterpret_cast<const char*>(sqlite3_column_text(statement, 5));
            data["rows"].push_back(row);
            data["product_count"] = data["product_count"].get<int>() + 1;
        }
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);

    try {
        inja::Environment environment((root / "templates").string());
        environment.set_html_autoescape(true);
        const auto page = environment.render_file((root / "templates" / "products.html").string(), data);
        return response("200 OK", "text/html; charset=utf-8", page);
    } catch (const std::exception& error) {
        std::cerr << "Products template rendering failed: " << error.what() << "\n";
        return response("500 Internal Server Error", "text/plain; charset=utf-8", std::string("Products template unavailable: ") + error.what() + "\n");
    }
}

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

std::string product_mutation(const std::filesystem::path& root, std::string_view request, std::string_view path) {
    if (session_username(request).empty()) return redirect("/?error=Please%20sign%20in");
    if (!is_admin(root, request)) return redirect("/products-view?error=Administrator%20access%20required");

    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }

    const auto body_start = request.find("\r\n\r\n");
    const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4);
    sqlite3_stmt* statement = nullptr;
    int result = SQLITE_ERROR;
    if (path == "/products/create") {
        result = sqlite3_prepare_v2(database, "INSERT INTO products (name, sku, price, stock_quantity) VALUES (?1, ?2, ?3, ?4)", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, form_value(body, "name").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, form_value(body, "sku").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(statement, 3, std::strtod(form_value(body, "price").c_str(), nullptr));
            sqlite3_bind_int(statement, 4, static_cast<int>(std::strtol(form_value(body, "stock_quantity").c_str(), nullptr, 10)));
        }
    } else if (path == "/products/update") {
        result = sqlite3_prepare_v2(database, "UPDATE products SET name = ?1, sku = ?2, price = ?3, stock_quantity = ?4 WHERE id = ?5", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, form_value(body, "name").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(statement, 2, form_value(body, "sku").c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(statement, 3, std::strtod(form_value(body, "price").c_str(), nullptr));
            sqlite3_bind_int(statement, 4, static_cast<int>(std::strtol(form_value(body, "stock_quantity").c_str(), nullptr, 10)));
            sqlite3_bind_int(statement, 5, static_cast<int>(std::strtol(form_value(body, "id").c_str(), nullptr, 10)));
        }
    } else if (path.rfind("/products/delete/", 0) == 0) {
        result = sqlite3_prepare_v2(database, "DELETE FROM products WHERE id = ?1", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, static_cast<int>(std::strtol(std::string(path.substr(std::string("/products/delete/").size())).c_str(), nullptr, 10)));
        }
    }

    if (result == SQLITE_OK) {
        sqlite3_exec(database, "BEGIN", nullptr, nullptr, nullptr);
        result = sqlite3_step(statement);
        if (result == SQLITE_DONE) sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr);
        else sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    if (result != SQLITE_DONE) return redirect("/products-view?error=Product%20operation%20failed");
    return redirect("/products-view");
}

std::string entities_result(const std::filesystem::path& root, std::string_view request) {
    const auto username = session_username(request);
    if (username.empty()) return redirect("/?error=Please%20sign%20in");
    const std::vector<std::string> columns = {"id", "entity_type", "name", "email", "created_at"};
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
    nlohmann::json data;
    data["username"] = username; data["role"] = role;
    for (const auto& column : columns) data["columns"].push_back(column);
    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT e.id, et.entity, e.name, COALESCE(e.email, ''), e.created_at FROM entities e INNER JOIN entity_type et ON e.entity_type = et.id ORDER BY e.name";
    if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) {
            data["rows"].push_back({{"id", sqlite3_column_int(statement, 0)}, {"entity_type", reinterpret_cast<const char*>(sqlite3_column_text(statement, 1))}, {"name", reinterpret_cast<const char*>(sqlite3_column_text(statement, 2))}, {"email", reinterpret_cast<const char*>(sqlite3_column_text(statement, 3))}, {"created_at", reinterpret_cast<const char*>(sqlite3_column_text(statement, 4))}});
        }
    }
    sqlite3_finalize(statement); sqlite3_close(database);
    try {
        inja::Environment environment((root / "templates").string()); environment.set_html_autoescape(true);
        return response("200 OK", "text/html; charset=utf-8", environment.render_file((root / "templates" / "entities.html").string(), data));
    } catch (const std::exception& error) {
        std::cerr << "Entities template rendering failed: " << error.what() << "\n";
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Entities template unavailable\n");
    }
}

std::string entity_mutation(const std::filesystem::path& root, std::string_view request, std::string_view path) {
    if (session_username(request).empty()) return redirect("/?error=Please%20sign%20in");
    if (!is_admin(root, request)) return redirect("/entities-view?error=Administrator%20access%20required");
    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    const auto body_start = request.find("\r\n\r\n"); const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4);
    sqlite3_stmt* statement = nullptr; int result = SQLITE_ERROR;
    if (path == "/entities/create") {
        result = sqlite3_prepare_v2(database, "INSERT INTO entities (entity_type, name, email) SELECT id, ?2, ?3 FROM entity_type WHERE entity = ?1", -1, &statement, nullptr);
        if (result == SQLITE_OK) { const auto type=form_value(body,"entity_type"), name=form_value(body,"name"), email=form_value(body,"email"); sqlite3_bind_text(statement,1,type.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,email.c_str(),-1,SQLITE_TRANSIENT); }
    } else if (path == "/entities/update") {
        result = sqlite3_prepare_v2(database, "UPDATE entities SET entity_type = (SELECT id FROM entity_type WHERE entity = ?1), name = ?2, email = ?3 WHERE id = ?4", -1, &statement, nullptr);
        if (result == SQLITE_OK) { const auto type=form_value(body,"entity_type"), name=form_value(body,"name"), email=form_value(body,"email"); sqlite3_bind_text(statement,1,type.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,name.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,email.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(statement,4,static_cast<int>(std::strtol(form_value(body,"id").c_str(),nullptr,10))); }
    } else if (path.rfind("/entities/delete/", 0) == 0) {
        result = sqlite3_prepare_v2(database, "DELETE FROM entities WHERE id = ?1", -1, &statement, nullptr);
        if (result == SQLITE_OK) sqlite3_bind_int(statement,1,static_cast<int>(std::strtol(std::string(path.substr(std::string("/entities/delete/").size())).c_str(),nullptr,10)));
    }
    if (result == SQLITE_OK) { sqlite3_exec(database,"BEGIN",nullptr,nullptr,nullptr); result=sqlite3_step(statement); if(result==SQLITE_DONE) sqlite3_exec(database,"COMMIT",nullptr,nullptr,nullptr); else sqlite3_exec(database,"ROLLBACK",nullptr,nullptr,nullptr); }
    sqlite3_finalize(statement); sqlite3_close(database); return result == SQLITE_DONE ? redirect("/entities-view") : redirect("/entities-view?error=Entity%20operation%20failed");
}

std::string users_result(const std::filesystem::path& root, std::string_view request) {
    if (session_username(request).empty()) return redirect("/?error=Please%20sign%20in");
    if (!is_admin(root, request)) return redirect("/dashboard?error=Administrator%20access%20required");
    sqlite3* database = nullptr; const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    nlohmann::json data; data["username"] = session_username(request);
    const std::vector<std::string> columns = {"id", "username", "email", "role", "created_at"}; for (const auto& column : columns) data["columns"].push_back(column);
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(database, "SELECT id, username, email, role, created_at FROM users ORDER BY username", -1, &statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(statement) == SQLITE_ROW) data["rows"].push_back({{"id", sqlite3_column_int(statement,0)}, {"username", reinterpret_cast<const char*>(sqlite3_column_text(statement,1))}, {"email", reinterpret_cast<const char*>(sqlite3_column_text(statement,2))}, {"role", reinterpret_cast<const char*>(sqlite3_column_text(statement,3))}, {"created_at", reinterpret_cast<const char*>(sqlite3_column_text(statement,4))}});
    }
    sqlite3_finalize(statement); sqlite3_close(database);
    try { inja::Environment environment((root / "templates").string()); environment.set_html_autoescape(true); return response("200 OK", "text/html; charset=utf-8", environment.render_file((root / "templates" / "users.html").string(), data)); }
    catch (const std::exception& error) { std::cerr << "Users template rendering failed: " << error.what() << "\n"; return response("500 Internal Server Error", "text/plain; charset=utf-8", "Users template unavailable\n"); }
}

std::string user_mutation(const std::filesystem::path& root, std::string_view request, std::string_view path) {
    if (session_username(request).empty()) return redirect("/?error=Please%20sign%20in");
    if (!is_admin(root, request)) return redirect("/dashboard?error=Administrator%20access%20required");
    sqlite3* database = nullptr; const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    const auto body_start = request.find("\r\n\r\n"); const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4);
    sqlite3_stmt* statement = nullptr; int result = SQLITE_ERROR;
    const auto username=form_value(body,"username"), email=form_value(body,"email"), role=form_value(body,"role"), password=form_value(body,"password");
    if (path == "/users/create") {
        result = sqlite3_prepare_v2(database, "INSERT INTO users (username, email, password_hash, role) VALUES (?1, ?2, ?3, ?4)", -1, &statement, nullptr);
        if (result == SQLITE_OK) { const auto hash=bcrypt::generateHash(password); sqlite3_bind_text(statement,1,username.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,email.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,hash.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,4,role.c_str(),-1,SQLITE_TRANSIENT); }
    } else if (path == "/users/update") {
        const bool change_password = !password.empty();
        const char* sql = change_password ? "UPDATE users SET username=?1, email=?2, role=?3, password_hash=?4 WHERE id=?5" : "UPDATE users SET username=?1, email=?2, role=?3 WHERE id=?4";
        result = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
        if (result == SQLITE_OK) { sqlite3_bind_text(statement,1,username.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,2,email.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(statement,3,role.c_str(),-1,SQLITE_TRANSIENT); if(change_password){const auto hash=bcrypt::generateHash(password); sqlite3_bind_text(statement,4,hash.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(statement,5,static_cast<int>(std::strtol(form_value(body,"id").c_str(),nullptr,10)));} else sqlite3_bind_int(statement,4,static_cast<int>(std::strtol(form_value(body,"id").c_str(),nullptr,10))); }
    } else if (path.rfind("/users/delete/",0)==0) {
        result = sqlite3_prepare_v2(database, "DELETE FROM users WHERE id=?1 AND username != ?2", -1, &statement, nullptr);
        if (result == SQLITE_OK) { sqlite3_bind_int(statement,1,static_cast<int>(std::strtol(std::string(path.substr(std::string("/users/delete/").size())).c_str(),nullptr,10))); const auto current=session_username(request); sqlite3_bind_text(statement,2,current.c_str(),-1,SQLITE_TRANSIENT); }
    }
    if(result==SQLITE_OK){sqlite3_exec(database,"BEGIN",nullptr,nullptr,nullptr);result=sqlite3_step(statement);if(result==SQLITE_DONE)sqlite3_exec(database,"COMMIT",nullptr,nullptr,nullptr);else sqlite3_exec(database,"ROLLBACK",nullptr,nullptr,nullptr);} sqlite3_finalize(statement);sqlite3_close(database); return result==SQLITE_DONE?redirect("/users-view"):redirect("/users-view?error=User%20operation%20failed");
}

bool can_checkout(const std::filesystem::path& root, std::string_view request) {
    const auto username = session_username(request); if (username.empty()) return false;
    sqlite3* database=nullptr; const auto file=(root/"data"/"system_control_center.db").string(); if(sqlite3_open_v2(file.c_str(),&database,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return false;
    sqlite3_stmt* s=nullptr; bool allowed=false; if(sqlite3_prepare_v2(database,"SELECT role FROM users WHERE username=?1",-1,&s,nullptr)==SQLITE_OK){sqlite3_bind_text(s,1,username.c_str(),-1,SQLITE_TRANSIENT);if(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_text(s,0)) {auto r=std::string(reinterpret_cast<const char*>(sqlite3_column_text(s,0)));allowed=r=="admin"||r=="operator";}} sqlite3_finalize(s);sqlite3_close(database);return allowed;
}

std::string checkout_result(const std::filesystem::path& root, std::string_view request) {
    const auto username=session_username(request); if(username.empty())return redirect("/?error=Please%20sign%20in");
    sqlite3* db=nullptr; const auto file=(root/"data"/"system_control_center.db").string(); if(sqlite3_open_v2(file.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return response("500 Internal Server Error","text/plain; charset=utf-8","Database unavailable\n");
    nlohmann::json data; data["role"]="viewer"; data["username"]=username;
    sqlite3_stmt* role=nullptr;if(sqlite3_prepare_v2(db,"SELECT role FROM users WHERE username=?1",-1,&role,nullptr)==SQLITE_OK){sqlite3_bind_text(role,1,username.c_str(),-1,SQLITE_TRANSIENT);if(sqlite3_step(role)==SQLITE_ROW)data["role"]=reinterpret_cast<const char*>(sqlite3_column_text(role,0));}sqlite3_finalize(role);
    sqlite3_stmt* s=nullptr;if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,et.entity FROM entities e JOIN entity_type et ON e.entity_type=et.id ORDER BY e.name",-1,&s,nullptr)==SQLITE_OK)while(sqlite3_step(s)==SQLITE_ROW)data["customers"].push_back({{"id",sqlite3_column_int(s,0)},{"name",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"entity_type",reinterpret_cast<const char*>(sqlite3_column_text(s,2))}});sqlite3_finalize(s);
    if(sqlite3_prepare_v2(db,"SELECT id,name,price,stock_quantity FROM products WHERE stock_quantity>0 ORDER BY name",-1,&s,nullptr)==SQLITE_OK)while(sqlite3_step(s)==SQLITE_ROW)data["products"].push_back({{"id",sqlite3_column_int(s,0)},{"name",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"price",sqlite3_column_double(s,2)},{"stock_quantity",sqlite3_column_int(s,3)}});sqlite3_finalize(s);sqlite3_close(db);
    try{inja::Environment env((root/"templates").string());env.set_html_autoescape(true);return response("200 OK","text/html; charset=utf-8",env.render_file((root/"templates"/"checkout.html").string(),data));}catch(...){return response("500 Internal Server Error","text/plain; charset=utf-8","Checkout template unavailable\n");}
}

std::string checkout_create(const std::filesystem::path& root, std::string_view request) {
    if (!can_checkout(root, request)) return redirect("/?error=Operator%20access%20required");
    const auto body_start = request.find("\r\n\r\n");
    const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4);
    const int entity = static_cast<int>(std::strtol(form_value(body, "entity_id").c_str(), nullptr, 10));
    const auto product_values = form_values(body, "product_id");
    const auto quantity_values = form_values(body, "quantity");
    if (entity <= 0 || product_values.empty() || product_values.size() != quantity_values.size()) return redirect("/checkout?error=Invalid%20order%20data");

    struct Line { int product_id; int quantity; };
    std::vector<Line> lines;
    for (std::size_t index = 0; index < product_values.size(); ++index) {
        const int product_id = static_cast<int>(std::strtol(product_values[index].c_str(), nullptr, 10));
        const int quantity = static_cast<int>(std::strtol(quantity_values[index].c_str(), nullptr, 10));
        if (product_id <= 0 || quantity <= 0) return redirect("/checkout?error=Invalid%20order%20data");
        lines.push_back({product_id, quantity});
    }

    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }
    sqlite3_exec(database, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* statement = nullptr;
    int result = SQLITE_OK;
    std::vector<double> prices;
    for (const auto& line : lines) {
        result = sqlite3_prepare_v2(database, "SELECT price, stock_quantity FROM products WHERE id = ?1", -1, &statement, nullptr);
        double price = 0.0;
        int stock = 0;
        if (result == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, line.product_id);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                price = sqlite3_column_double(statement, 0);
                stock = sqlite3_column_int(statement, 1);
            } else result = SQLITE_ERROR;
        }
        sqlite3_finalize(statement);
        if (result != SQLITE_OK || stock < line.quantity) { result = SQLITE_ERROR; break; }
        prices.push_back(price);
    }

    sqlite3_int64 created_order_id = 0;
    if (result == SQLITE_OK) {
        result = sqlite3_prepare_v2(database, "INSERT INTO orders(entity_id, status) VALUES(?1, 'COMPLETED')", -1, &statement, nullptr);
        if (result == SQLITE_OK) { sqlite3_bind_int(statement, 1, entity); result = sqlite3_step(statement); }
        sqlite3_finalize(statement);
        created_order_id = sqlite3_last_insert_rowid(database);
    }
    if (result == SQLITE_DONE) {
        for (std::size_t index = 0; index < lines.size(); ++index) {
            result = sqlite3_prepare_v2(database, "INSERT INTO order_items(order_id, product_id, quantity, unit_price) VALUES(?1, ?2, ?3, ?4)", -1, &statement, nullptr);
            if (result == SQLITE_OK) {
                sqlite3_bind_int64(statement, 1, created_order_id);
                sqlite3_bind_int(statement, 2, lines[index].product_id);
                sqlite3_bind_int(statement, 3, lines[index].quantity);
                sqlite3_bind_double(statement, 4, prices[index]);
                result = sqlite3_step(statement);
            }
            sqlite3_finalize(statement);
            if (result != SQLITE_DONE) break;
            result = sqlite3_prepare_v2(database, "UPDATE products SET stock_quantity = stock_quantity - ?1 WHERE id = ?2 AND stock_quantity >= ?1", -1, &statement, nullptr);
            if (result == SQLITE_OK) {
                sqlite3_bind_int(statement, 1, lines[index].quantity);
                sqlite3_bind_int(statement, 2, lines[index].product_id);
                result = sqlite3_step(statement);
                if (result == SQLITE_DONE && sqlite3_changes(database) != 1) result = SQLITE_ERROR;
            }
            sqlite3_finalize(statement);
            if (result != SQLITE_DONE) break;
        }
    }
    if (result == SQLITE_DONE) sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr);
    else sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
    sqlite3_close(database);
    return result == SQLITE_DONE ? redirect("/orders/" + std::to_string(created_order_id)) : redirect("/checkout?error=Insufficient%20stock%20or%20invalid%20order");
}

std::string orders_result(const std::filesystem::path& root, std::string_view request) {
    if(session_username(request).empty())return redirect("/?error=Please%20sign%20in");sqlite3*db=nullptr;const auto file=(root/"data"/"system_control_center.db").string();if(sqlite3_open_v2(file.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return response("500 Internal Server Error","text/plain; charset=utf-8","Database unavailable\n");nlohmann::json data;sqlite3_stmt*s=nullptr;const char*sql="SELECT o.id,e.name,o.status,o.created_at,COALESCE(SUM(oi.quantity*oi.unit_price),0) FROM orders o JOIN entities e ON o.entity_id=e.id LEFT JOIN order_items oi ON oi.order_id=o.id GROUP BY o.id,e.name,o.status,o.created_at ORDER BY o.created_at DESC";if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK)while(sqlite3_step(s)==SQLITE_ROW)data["orders"].push_back({{"order_id",sqlite3_column_int(s,0)},{"customer_name",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"status",reinterpret_cast<const char*>(sqlite3_column_text(s,2))},{"created_at",reinterpret_cast<const char*>(sqlite3_column_text(s,3))},{"order_total",sqlite3_column_double(s,4)}});sqlite3_finalize(s);sqlite3_close(db);try{inja::Environment env((root/"templates").string());env.set_html_autoescape(true);return response("200 OK","text/html; charset=utf-8",env.render_file((root/"templates"/"orders.html").string(),data));}catch(...){return response("500 Internal Server Error","text/plain; charset=utf-8","Orders template unavailable\n");}
}

std::string order_detail_result(const std::filesystem::path& root, std::string_view request, int order_id) {
    if(session_username(request).empty())return redirect("/?error=Please%20sign%20in");sqlite3*db=nullptr;const auto file=(root/"data"/"system_control_center.db").string();if(sqlite3_open_v2(file.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)!=SQLITE_OK)return response("500 Internal Server Error","text/plain; charset=utf-8","Database unavailable\n");nlohmann::json data;data["back_url"]="/orders-view";data["back_label"]="Back to Orders";sqlite3_stmt*s=nullptr;const char*sql="SELECT o.id,e.name,o.status,o.created_at,COALESCE(SUM(oi.quantity*oi.unit_price),0) FROM orders o JOIN entities e ON o.entity_id=e.id LEFT JOIN order_items oi ON oi.order_id=o.id WHERE o.id=?1 GROUP BY o.id,e.name,o.status,o.created_at";if(sqlite3_prepare_v2(db,sql,-1,&s,nullptr)==SQLITE_OK){sqlite3_bind_int(s,1,order_id);if(sqlite3_step(s)==SQLITE_ROW)data["order"]={{"order_id",sqlite3_column_int(s,0)},{"customer_name",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"status",reinterpret_cast<const char*>(sqlite3_column_text(s,2))},{"created_at",reinterpret_cast<const char*>(sqlite3_column_text(s,3))},{"order_total",sqlite3_column_double(s,4)}};}sqlite3_finalize(s);if(!data.contains("order")){sqlite3_close(db);return response("404 Not Found","text/plain; charset=utf-8","Order not found\n");}if(sqlite3_prepare_v2(db,"SELECT p.name,p.sku,oi.quantity,oi.unit_price,oi.quantity*oi.unit_price FROM order_items oi JOIN products p ON p.id=oi.product_id WHERE oi.order_id=?1 ORDER BY oi.id",-1,&s,nullptr)==SQLITE_OK){sqlite3_bind_int(s,1,order_id);while(sqlite3_step(s)==SQLITE_ROW)data["items"].push_back({{"product_name",reinterpret_cast<const char*>(sqlite3_column_text(s,0))},{"sku",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"quantity",sqlite3_column_int(s,2)},{"unit_price",sqlite3_column_double(s,3)},{"row_total",sqlite3_column_double(s,4)}});}sqlite3_finalize(s);sqlite3_close(db);try{inja::Environment env((root/"templates").string());env.set_html_autoescape(true);return response("200 OK","text/html; charset=utf-8",env.render_file((root/"templates"/"order_detail.html").string(),data));}catch(...){return response("500 Internal Server Error","text/plain; charset=utf-8","Order template unavailable\n");}
}

std::string refund_order(const std::filesystem::path& root, std::string_view request, int order_id) {
    if (!can_checkout(root, request)) return redirect("/?error=Operator%20access%20required");

    const auto body_start = request.find("\r\n\r\n");
    const auto body = body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4);
    const auto reason = form_value(body, "reason");
    const auto username = session_username(request);
    const auto database_path = (root / "data" / "system_control_center.db").string();
    sqlite3* database = nullptr;
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }

    sqlite3_exec(database, "BEGIN", nullptr, nullptr, nullptr);
    sqlite3_stmt* statement = nullptr;
    int result = sqlite3_prepare_v2(database,
        "SELECT status, COALESCE(SUM(oi.quantity * oi.unit_price), 0) "
        "FROM orders o LEFT JOIN order_items oi ON oi.order_id = o.id "
        "WHERE o.id = ?1 GROUP BY o.id, o.status", -1, &statement, nullptr);
    double amount = 0.0;
    std::string status;
    if (result == SQLITE_OK) {
        sqlite3_bind_int(statement, 1, order_id);
        if (sqlite3_step(statement) == SQLITE_ROW) {
            const auto* status_text = sqlite3_column_text(statement, 0);
            status = status_text == nullptr ? "" : reinterpret_cast<const char*>(status_text);
            amount = sqlite3_column_double(statement, 1);
        } else {
            result = SQLITE_ERROR;
        }
    }
    sqlite3_finalize(statement);

    sqlite3_int64 refund_id = 0;
    if (result == SQLITE_OK && status == "COMPLETED") {
        result = sqlite3_prepare_v2(database,
            "INSERT INTO order_refunds(order_id, refunded_by, reason, amount) "
            "SELECT ?1, id, ?2, ?3 FROM users WHERE username = ?4", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, order_id);
            const auto refund_reason = reason.empty() ? std::string("Customer refund") : reason;
            sqlite3_bind_text(statement, 2, refund_reason.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_double(statement, 3, amount);
            sqlite3_bind_text(statement, 4, username.c_str(), -1, SQLITE_TRANSIENT);
            result = sqlite3_step(statement);
        }
        sqlite3_finalize(statement);
        refund_id = sqlite3_last_insert_rowid(database);
    }

    if (result == SQLITE_DONE) {
        result = sqlite3_prepare_v2(database,
            "SELECT id, product_id, quantity, unit_price FROM order_items WHERE order_id = ?1", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, order_id);
            while (sqlite3_step(statement) == SQLITE_ROW) {
                const int item_id = sqlite3_column_int(statement, 0);
                const int product_id = sqlite3_column_int(statement, 1);
                const int quantity = sqlite3_column_int(statement, 2);
                const double unit_price = sqlite3_column_double(statement, 3);

                sqlite3_stmt* item_statement = nullptr;
                if (sqlite3_prepare_v2(database,
                    "INSERT INTO order_refund_items(refund_id, order_item_id, quantity, unit_price) VALUES(?1, ?2, ?3, ?4)",
                    -1, &item_statement, nullptr) != SQLITE_OK) {
                    result = SQLITE_ERROR;
                    break;
                }
                sqlite3_bind_int64(item_statement, 1, refund_id);
                sqlite3_bind_int(item_statement, 2, item_id);
                sqlite3_bind_int(item_statement, 3, quantity);
                sqlite3_bind_double(item_statement, 4, unit_price);
                if (sqlite3_step(item_statement) != SQLITE_DONE) result = SQLITE_ERROR;
                sqlite3_finalize(item_statement);
                if (result != SQLITE_DONE) break;

                sqlite3_stmt* stock_statement = nullptr;
                if (sqlite3_prepare_v2(database,
                    "UPDATE products SET stock_quantity = stock_quantity + ?1 WHERE id = ?2", -1, &stock_statement, nullptr) != SQLITE_OK) {
                    result = SQLITE_ERROR;
                    break;
                }
                sqlite3_bind_int(stock_statement, 1, quantity);
                sqlite3_bind_int(stock_statement, 2, product_id);
                if (sqlite3_step(stock_statement) != SQLITE_DONE || sqlite3_changes(database) != 1) result = SQLITE_ERROR;
                sqlite3_finalize(stock_statement);
                if (result != SQLITE_DONE) break;
            }
        }
        sqlite3_finalize(statement);
    }

    if (result == SQLITE_DONE) {
        result = sqlite3_prepare_v2(database, "UPDATE orders SET status = 'REFUNDED' WHERE id = ?1", -1, &statement, nullptr);
        if (result == SQLITE_OK) {
            sqlite3_bind_int(statement, 1, order_id);
            result = sqlite3_step(statement);
        }
        sqlite3_finalize(statement);
    }

    if (result == SQLITE_DONE) {
        sqlite3_exec(database, "COMMIT", nullptr, nullptr, nullptr);
    } else {
        sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    sqlite3_close(database);
    return redirect("/orders/" + std::to_string(order_id));
}

std::string login_result(const std::filesystem::path& root, std::string_view body) {
    const auto username = form_value(body, "username");
    const auto password = form_value(body, "password");
    sqlite3* database = nullptr;
    const auto database_path = (root / "data" / "system_control_center.db").string();
    if (sqlite3_open_v2(database_path.c_str(), &database, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
        if (database != nullptr) sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Database unavailable\n");
    }

    sqlite3_stmt* statement = nullptr;
    const char* sql = "SELECT password_hash, role FROM users WHERE username = ?1";
    const auto prepared = sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
    if (prepared != SQLITE_OK) {
        sqlite3_close(database);
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Authentication unavailable\n");
    }
    sqlite3_bind_text(statement, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::string password_hash;
    std::string role;
    if (sqlite3_step(statement) == SQLITE_ROW) {
        const auto* hash = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
        const auto* stored_role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
        if (hash != nullptr) password_hash = hash;
        if (stored_role != nullptr) role = stored_role;
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

std::string dashboard_result(const std::filesystem::path& root, std::string_view request) {
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
            entity_breakdown.emplace_back(
                reinterpret_cast<const char*>(sqlite3_column_text(entity_statement, 0)),
                sqlite3_column_int(entity_statement, 1));
        }
    }
    sqlite3_finalize(entity_statement);

    struct RecentOrder { int id; std::string customer; std::string status; double total; };
    std::vector<RecentOrder> recent_orders;
    sqlite3_stmt* order_statement = nullptr;
    const char* order_sql = "SELECT o.id, e.name, o.status, COALESCE(SUM(oi.quantity * oi.unit_price), 0) FROM orders o INNER JOIN entities e ON o.entity_id = e.id LEFT JOIN order_items oi ON oi.order_id = o.id GROUP BY o.id, e.name, o.status, o.created_at ORDER BY o.created_at DESC LIMIT 5";
    if (sqlite3_prepare_v2(database, order_sql, -1, &order_statement, nullptr) == SQLITE_OK) {
        while (sqlite3_step(order_statement) == SQLITE_ROW) {
            recent_orders.push_back({
                sqlite3_column_int(order_statement, 0),
                reinterpret_cast<const char*>(sqlite3_column_text(order_statement, 1)),
                reinterpret_cast<const char*>(sqlite3_column_text(order_statement, 2)),
                sqlite3_column_double(order_statement, 3)});
        }
    }
    sqlite3_finalize(order_statement);
    sqlite3_close(database);

    nlohmann::json data;
    data["username"] = username;
    data["role"] = role;
    for (const auto& item : entity_breakdown) {
        data["entity_breakdown"].push_back({{"entity_type", item.first}, {"qty", item.second}});
    }
    for (const auto& order : recent_orders) {
        data["recent_orders"].push_back({{"order_id", order.id}, {"customer_name", order.customer}, {"status", order.status}, {"order_total", order.total}});
    }

    try {
        inja::Environment environment((root / "templates").string());
        environment.set_html_autoescape(true);
        const auto page = environment.render_file((root / "templates" / "dashboard.html").string(), data);
        return response("200 OK", "text/html; charset=utf-8", page);
    } catch (const std::exception& error) {
        std::cerr << "Template rendering failed: " << error.what() << "\n";
        return response("500 Internal Server Error", "text/plain; charset=utf-8", "Dashboard template unavailable\n");
    }
}

std::string handle_request(std::string_view request, const std::filesystem::path& root) {
    const auto path = request_path(request);
    if (path.empty()) return response("400 Bad Request", "text/plain; charset=utf-8", "Bad request\n");

    if (path == "/health") {
        return response("200 OK", "application/json; charset=utf-8", "{\"status\":\"ok\"}\n");
    }

    if (request_method(request) == "POST" && path == "/login") {
        const auto body_start = request.find("\r\n\r\n");
        return login_result(root, body_start == std::string_view::npos ? std::string_view{} : request.substr(body_start + 4));
    }

    if (request_method(request) == "GET" && path == "/") {
        return login_page(root, request);
    }

    if (request_method(request) == "GET" && path == "/products-view") {
        return products_result(root, request);
    }

    if (request_method(request) == "POST" &&
        (path == "/products/create" || path == "/products/update" || path.rfind("/products/delete/", 0) == 0)) {
        return product_mutation(root, request, path);
    }

    if (request_method(request) == "GET" && path == "/entities-view") return entities_result(root, request);
    if (request_method(request) == "POST" && (path == "/entities/create" || path == "/entities/update" || path.rfind("/entities/delete/", 0) == 0)) return entity_mutation(root, request, path);
    if (request_method(request) == "GET" && path == "/users-view") return users_result(root, request);
    if (request_method(request) == "POST" && (path == "/users/create" || path == "/users/update" || path.rfind("/users/delete/", 0) == 0)) return user_mutation(root, request, path);
    if (request_method(request) == "GET" && path == "/checkout") return checkout_result(root, request);
    if (request_method(request) == "POST" && path == "/checkout/create") return checkout_create(root, request);
    if (request_method(request) == "GET" && path == "/orders-view") return orders_result(root, request);
    if (request_method(request) == "GET" && path.rfind("/orders/", 0) == 0) return order_detail_result(root, request, static_cast<int>(std::strtol(std::string(path.substr(8)).c_str(), nullptr, 10)));
    if (request_method(request) == "POST" && path.rfind("/orders/", 0) == 0 && path.find("/refund") != std::string::npos) return refund_order(root, request, static_cast<int>(std::strtol(std::string(path.substr(8, path.find("/refund") - 8)).c_str(), nullptr, 10)));

    if (request_method(request) == "GET" && path == "/dashboard") {
        return dashboard_result(root, request);
    }

    if (request_method(request) == "GET" && path == "/logout") {
        const auto token = session_token(request);
        if (!token.empty()) {
            std::lock_guard<std::mutex> lock(sessions_mutex);
            sessions.erase(token);
        }
        return response("303 See Other", "text/plain; charset=utf-8", "",
                        "Location: /\r\nSet-Cookie: scc_session=; Max-Age=0; HttpOnly; SameSite=Lax\r\n");
    }

    if (path.find("..") != std::string::npos) {
        return response("403 Forbidden", "text/plain; charset=utf-8", "Forbidden\n");
    }

    std::string relative = path == "/" ? "/templates/login.html" : path;
    if (relative.rfind("/static/", 0) != 0 && relative.rfind("/templates/", 0) != 0) {
        return response("404 Not Found", "text/plain; charset=utf-8", "Not found\n");
    }

    const auto file_path = root / relative.substr(1);
    const auto body = read_file(file_path);
    if (body.empty()) return response("404 Not Found", "text/plain; charset=utf-8", "Not found\n");
    return response("200 OK", content_type(file_path), body);
}

std::string receive_request(Socket client) {
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

} // namespace

HttpServer::HttpServer(unsigned short port, std::filesystem::path web_root)
    : port_(port), web_root_(std::move(web_root)) {}

std::string HttpServer::handle_request(const std::string& request) const {
    return ::handle_request(request, web_root_);
}

int HttpServer::run() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        std::cerr << "Unable to initialize Winsock\n";
        return 1;
    }
#endif

    const Socket server = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server == invalid_socket) {
        std::cerr << "Unable to create server socket\n";
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port_);
    if (bind(server, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(server, 8) != 0) {
        std::cerr << "Unable to bind or listen on 127.0.0.1:" << port_ << "\n";
        close_socket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "System Control Center C++ web server\n"
              << "Listening at http://127.0.0.1:" << port_ << "\n"
              << "Press Ctrl+C to stop.\n";

    for (;;) {
        sockaddr_in client_address{};
#ifdef _WIN32
        int client_length = sizeof(client_address);
#else
        socklen_t client_length = sizeof(client_address);
#endif
        const Socket client = accept(server, reinterpret_cast<sockaddr*>(&client_address), &client_length);
        if (client == invalid_socket) continue;

        const auto request = receive_request(client);
        if (!request.empty()) {
            const auto result = handle_request(request);
            send(client, result.data(), static_cast<int>(result.size()), 0);
        }
        close_socket(client);
    }
}
