#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string entity_list_route(const std::filesystem::path& root, std::string_view request);
std::string entity_mutation_route(const std::filesystem::path& root, std::string_view request, std::string_view path);
std::string user_list_route(const std::filesystem::path& root, std::string_view request);
std::string user_mutation_route(const std::filesystem::path& root, std::string_view request, std::string_view path);
