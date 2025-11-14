# --- nlohmann_json ---
#
# Fetches the nlohmann_json library.
#
include(FetchContent)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
    PATCH_COMMAND git apply "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json.patch"
)

FetchContent_MakeAvailable(nlohmann_json)
