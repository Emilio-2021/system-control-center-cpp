#pragma once

#include <string>
#include <string_view>

std::string redirect(std::string_view location);
std::string response(std::string_view status, std::string_view type,
                     std::string_view body, std::string_view extra_headers = {});
