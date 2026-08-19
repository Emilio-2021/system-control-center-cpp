#pragma once

#include <string>
#include <string_view>

std::string create_session(const std::string& username);
std::string session_username(std::string_view request);
void erase_session(std::string_view request);
