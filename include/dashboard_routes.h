#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string dashboard_route(const std::filesystem::path& root, std::string_view request);
