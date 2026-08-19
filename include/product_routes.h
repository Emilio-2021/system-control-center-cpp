#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string product_list_route(const std::filesystem::path& root, std::string_view request);
std::string product_mutation_route(const std::filesystem::path& root, std::string_view request, std::string_view path);
