cmake_minimum_required(VERSION 3.20)

project(SystemControlCenterCpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(system_control_center
    src/main.cpp
    src/http_server.cpp
    src/inventory.cpp
    third_party/sqlite/sqlite3.c
    third_party/bcrypt/bcrypt.cpp
    third_party/bcrypt/blowfish.cpp
)

set_target_properties(system_control_center PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

target_include_directories(system_control_center PRIVATE
    include
    third_party/sqlite
    third_party/bcrypt
    third_party/inja
    third_party/json
)

if (WIN32)
    target_link_libraries(system_control_center PRIVATE ws2_32)
endif()

if (MSVC)
    target_compile_options(system_control_center PRIVATE /W4 /permissive-)
else()
    target_compile_options(system_control_center PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(inventory_tests
    tests/inventory_tests.cpp
    src/inventory.cpp
)

target_include_directories(inventory_tests PRIVATE include)
add_test(NAME inventory_tests COMMAND inventory_tests)

add_executable(bcrypt_tests
    tests/bcrypt_tests.cpp
    third_party/bcrypt/bcrypt.cpp
    third_party/bcrypt/blowfish.cpp
)

target_include_directories(bcrypt_tests PRIVATE
    third_party/bcrypt
)
add_test(NAME bcrypt_tests COMMAND bcrypt_tests)
