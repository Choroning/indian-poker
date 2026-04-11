# Fetches header-only dependencies via FetchContent.
# System dependency (OpenSSL) is located via find_package in the main CMakeLists.

include(FetchContent)

# nlohmann/json — single-header JSON library.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# Catch2 v3 — test framework.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.4
)
FetchContent_MakeAvailable(Catch2)
