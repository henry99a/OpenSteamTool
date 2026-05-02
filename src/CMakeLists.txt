cmake_minimum_required(VERSION 3.20)
project(OpenSteamTool VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# Allow CMAKE_MSVC_RUNTIME_LIBRARY to control runtime selection for all targets,
# including dependencies pulled in via FetchContent.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# Static MSVC runtime everywhere, so the resulting DLL has no extra runtime
# dependencies. Must be set BEFORE FetchContent_MakeAvailable so the fetched
# deps (Lua, Detours, spdlog) inherit it.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# ---------------------------------------------------------------------------
# Dependency recipes (FetchContent-backed, cached at <repo>/.deps).
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Lua)
include(Detours)
include(Spdlog)
include(Nanopb)
include(Tomlplusplus)

# ---------------------------------------------------------------------------
# OpenSteamTool — the hook DLL injected into Steam (always 64-bit).
# ---------------------------------------------------------------------------
add_library(OpenSteamTool SHARED
    dllmain.cpp

    # Shared utilities
    Utils/AppTicket.cpp
    Utils/ByteSearch.cpp
    Utils/Log.cpp
    Utils/Config.cpp
    Utils/LuaConfig.cpp
    Utils/VehCommon.cpp
    Utils/WinHttp.cpp

    # Per-category hook modules
    Hook/HookManager.cpp
    Hook/Hooks_AccessToken.cpp
    Hook/Hooks_AppState.cpp
    Hook/Hooks_AppTicket.cpp
    Hook/Hooks_Decryption.cpp
    Hook/Hooks_IPC.cpp
    Hook/Hooks_KeyValues.cpp
    Hook/Hooks_Manifest.cpp
    Hook/Hooks_Misc.cpp
    Hook/Hooks_NetPacket.cpp
    Hook/Hooks_SteamUI.cpp
    Hook/Hooks_Package.cpp

    # nanopb pre-generated sources (regenerate with nanopb_generator.py if .proto changes)
    proto/steam_messages.pb.c
)

# Header search path: src root path
target_include_directories(OpenSteamTool PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/proto"
)

target_link_libraries(OpenSteamTool PRIVATE
    lua_static
    detours
    winhttp
    nanopb
    tomlplusplus::tomlplusplus
    $<$<CONFIG:Debug>:spdlog::spdlog>
)

# Logging is compiled in only for Debug; Release reduces LOG_* to no-ops.
target_compile_definitions(OpenSteamTool PRIVATE
    $<$<CONFIG:Debug>:OPENSTEAMTOOL_LOGGING_ENABLED>
)

# ---------------------------------------------------------------------------
# dwmapi.dll hijack — small loader DLL placed alongside Steam.
# ---------------------------------------------------------------------------
add_library(dwmapi SHARED
    dwmapi/dwmapi.cpp
)
