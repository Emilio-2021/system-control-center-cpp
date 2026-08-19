#pragma once

#include <string>

#ifdef _WIN32
#include <winsock2.h>
using HttpSocket = SOCKET;
constexpr HttpSocket invalid_http_socket = INVALID_SOCKET;
#else
using HttpSocket = int;
constexpr HttpSocket invalid_http_socket = -1;
#endif

void close_http_socket(HttpSocket socket);
std::string receive_http_request(HttpSocket client);
