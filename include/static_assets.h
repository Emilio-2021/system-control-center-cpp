#pragma once

#include <filesystem>
#include <string>

std::string content_type(const std::filesystem::path& path);
std::string read_file(const std::filesystem::path& path);
