#pragma once

#include <filesystem>
#include <string>
#include <string_view>

std::string checkout_route(const std::filesystem::path& root, std::string_view request);
std::string checkout_create_route(const std::filesystem::path& root, std::string_view request);
std::string orders_route(const std::filesystem::path& root, std::string_view request);
std::string order_detail_route(const std::filesystem::path& root, std::string_view request, int order_id);
std::string refund_route(const std::filesystem::path& root, std::string_view request, int order_id);
