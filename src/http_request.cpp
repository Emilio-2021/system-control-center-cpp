#include "http_request.h"

#include <cstdlib>

namespace {

std::string url_decode(std::string_view value) {
    std::string decoded;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') decoded.push_back(' ');
        else if (value[i] == '%' && i + 2 < value.size()) {
            const auto hex = value.substr(i + 1, 2);
            char* end = nullptr;
            const auto code = std::strtol(std::string(hex).c_str(), &end, 16);
            if (end != nullptr && *end == '\0') {
                decoded.push_back(static_cast<char>(code));
                i += 2;
            } else decoded.push_back(value[i]);
        } else decoded.push_back(value[i]);
    }
    return decoded;
}

} // namespace

std::string request_path(std::string_view request) {
    const auto first_line_end = request.find("\r\n");
    const auto first_line = request.substr(0, first_line_end);
    const auto first_space = first_line.find(' ');
    const auto second_space = first_line.find(' ', first_space + 1);
    if (first_space == std::string_view::npos || second_space == std::string_view::npos) return {};
    auto path = std::string(first_line.substr(first_space + 1, second_space - first_space - 1));
    const auto query_start = path.find('?');
    if (query_start != std::string::npos) path.resize(query_start);
    return path;
}

std::string request_method(std::string_view request) {
    const auto first_space = request.find(' ');
    return first_space == std::string_view::npos ? std::string{} : std::string(request.substr(0, first_space));
}

std::string form_value(std::string_view body, std::string_view key) {
    std::size_t start = 0;
    while (start < body.size()) {
        const auto end = body.find('&', start);
        const auto part = body.substr(start, end == std::string_view::npos ? body.size() - start : end - start);
        const auto equals = part.find('=');
        if (equals != std::string_view::npos && part.substr(0, equals) == key) return url_decode(part.substr(equals + 1));
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
    return query_start == std::string_view::npos ? std::string{} : form_value(target.substr(query_start + 1), key);
}
