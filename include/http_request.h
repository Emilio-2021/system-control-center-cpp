#pragma once

#include <string>
#include <string_view>
#include <vector>

std::string request_path(std::string_view request);
std::string request_method(std::string_view request);
std::string form_value(std::string_view body, std::string_view key);
std::vector<std::string> form_values(std::string_view body, std::string_view key);
std::string query_value(std::string_view request, std::string_view key);
