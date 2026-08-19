#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string auth_login_page(const std::filesystem::path& root, std::string_view request);
std::string auth_login_result(const std::filesystem::path& root, std::string_view body);
