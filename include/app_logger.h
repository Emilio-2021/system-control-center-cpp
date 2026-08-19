#pragma once

#include <filesystem>
#include <string_view>

void write_log(const std::filesystem::path& root, std::string_view level,
               std::string_view message);
