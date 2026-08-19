#include "session_store.h"

#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>

namespace {

std::mutex sessions_mutex;
std::unordered_map<std::string, std::string> sessions;

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

} // namespace

std::string create_session(const std::string& username) {
    std::random_device random;
    std::mt19937_64 generator(random());
    std::ostringstream token;
    token << std::hex << generator() << generator();
    const auto session_token_value = token.str();
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions[session_token_value] = username;
    return session_token_value;
}

std::string session_username(std::string_view request) {
    const auto token = session_token(request);
    if (token.empty()) return {};
    std::lock_guard<std::mutex> lock(sessions_mutex);
    const auto session = sessions.find(token);
    return session == sessions.end() ? std::string{} : session->second;
}

void erase_session(std::string_view request) {
    const auto token = session_token(request);
    if (token.empty()) return;
    std::lock_guard<std::mutex> lock(sessions_mutex);
    sessions.erase(token);
}
