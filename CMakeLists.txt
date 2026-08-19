cmake_minimum_required(VERSION 3.20)

project(SystemControlCenterCpp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(system_control_center
    src/main.cpp
    src/http_server.cpp
    src/auth_routes.cpp
    src/dashboard_routes.cpp
    src/product_routes.cpp
    src/admin_routes.cpp
    src/order_routes.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/http_transport.cpp
    src/static_assets.cpp
    src/session_store.cpp
    src/app_logger.cpp
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

add_executable(http_integration_tests
    tests/http_integration_tests.cpp
    src/http_server.cpp
    src/auth_routes.cpp
    src/dashboard_routes.cpp
    src/product_routes.cpp
    src/admin_routes.cpp
    src/order_routes.cpp
    src/http_request.cpp
    src/http_response.cpp
    src/http_transport.cpp
    src/static_assets.cpp
    src/session_store.cpp
    src/app_logger.cpp
    third_party/sqlite/sqlite3.c
    third_party/bcrypt/bcrypt.cpp
    third_party/bcrypt/blowfish.cpp
)

target_include_directories(http_integration_tests PRIVATE
    include
    third_party/sqlite
    third_party/bcrypt
    third_party/inja
    third_party/json
)

if (WIN32)
    target_link_libraries(http_integration_tests PRIVATE ws2_32)
endif()

if (MSVC)
    target_compile_options(http_integration_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(http_integration_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

target_compile_definitions(http_integration_tests PRIVATE
    SCC_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
add_test(NAME http_integration_tests COMMAND http_integration_tests)
