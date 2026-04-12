cmake_minimum_required(VERSION 3.20)
project(indian_poker CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Emit compile_commands.json so clangd / other LSP tooling can resolve
# FetchContent-provided headers (Catch2, nlohmann/json) from editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)

# System dependency.
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Fetched dependencies.
include(cmake/FetchDeps.cmake)

# Library of all non-main sources, used by both the server binary and tests.
add_library(ip_core STATIC
    util/version.cpp
    util/sha1.cpp
    util/base64.cpp
    net/tcp_listener.cpp
    net/connection.cpp
    net/event_loop.cpp
    net/ws_handshake.cpp
)
target_include_directories(ip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ip_core PUBLIC OpenSSL::Crypto nlohmann_json::nlohmann_json)

# Main server executable.
add_executable(ip_server main.cpp)
target_link_libraries(ip_server PRIVATE ip_core)

# Tests.
enable_testing()
add_subdirectory(../tests ${CMAKE_BINARY_DIR}/tests)
