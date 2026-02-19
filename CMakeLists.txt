cmake_minimum_required(VERSION 3.20)

project(LootTracker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via FetchContent ────────────────────────────────────────────
include(FetchContent)

# Nexus API header (provides AddonAPI_t, AddonDefinition_t, etc.)
FetchContent_Declare(
    nexus_api
    GIT_REPOSITORY https://github.com/RaidcoreGG/RCGG-lib-nexus-api.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nexus_api)

# NOTE: Mumble::LinkedMem / Mumble::Identity are defined inline in Shared.h
# (standard GW2 wiki layout) to avoid extra dependencies.

# Dear ImGui — same files Nexus compiles; we share its context at runtime.
# Pin this tag to match what Nexus ships (check Nexus THIRDPARTYSOFTWAREREADME
# and update accordingly if you see crashes on SetCurrentContext).
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.6
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# nlohmann/json — header-only JSON for settings & API responses
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(nlohmann_json)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/entry.cpp
    src/Shared.cpp
    src/Settings.cpp
    src/GW2Api.cpp
    src/LootSession.cpp
    src/UI.cpp

    # ImGui core — compiled inside our DLL, context shared with Nexus
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
)

add_library(LootTracker SHARED ${SOURCES})

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(LootTracker PRIVATE
    src
    ${nexus_api_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(LootTracker PRIVATE
    nlohmann_json::nlohmann_json
    winhttp      # WinHTTP for GW2 REST API calls (Windows built-in)
    winmm        # Multimedia timer (optional, for high-res timestamps)
)

# ── Compiler / linker flags ───────────────────────────────────────────────────
if(MSVC)
    # Remove default /W3 so we can set our own warning level
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    target_compile_options(LootTracker PRIVATE
        /W3         # Reasonable warnings
        /WX-        # Don't treat warnings as errors (imgui has some)
        /MP         # Parallel compilation
        /EHsc       # C++ exceptions
        /permissive-
        # Suppress noisy warnings from third-party headers
        /wd4100     # unreferenced formal parameter
        /wd4189     # local variable initialized but not referenced
    )

    # NOMINMAX prevents windows.h from defining min/max macros that clash with std::min/max
    target_compile_definitions(LootTracker PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # Export GetAddonDef via .def or __declspec(dllexport) — we use the latter
    # in entry.cpp; no .def file needed.
    target_link_options(LootTracker PRIVATE
        /SUBSYSTEM:WINDOWS
    )
endif()

# ── Output ────────────────────────────────────────────────────────────────────
set_target_properties(LootTracker PROPERTIES
    OUTPUT_NAME   "LootTracker"
    PREFIX        ""            # Don't prepend "lib" on MinGW
    SUFFIX        ".dll"
)
