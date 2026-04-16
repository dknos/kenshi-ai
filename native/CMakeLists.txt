cmake_minimum_required(VERSION 3.20)
project(kenshi_ai CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── KenshiLib ──────────────────────────────────────────────────────────────────
# Clone with:  git clone -b RE_Kenshi_mods https://github.com/KenshiReclaimer/KenshiLib deps/KenshiLib
set(KENSHILIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/KenshiLib" CACHE PATH "KenshiLib root")
if(NOT EXISTS "${KENSHILIB_DIR}/Include/kenshi/Character.h")
    message(FATAL_ERROR "KenshiLib not found at ${KENSHILIB_DIR}.\n"
        "Run: git clone -b RE_Kenshi_mods https://github.com/KenshiReclaimer/KenshiLib deps/KenshiLib")
endif()

# KenshiLib ships a pre-built static lib + headers
find_library(KENSHILIB_LIB KenshiLib
    PATHS "${KENSHILIB_DIR}/Lib/Release" "${KENSHILIB_DIR}/Lib"
    REQUIRED
)

# ── Source ─────────────────────────────────────────────────────────────────────
add_library(kenshi_ai SHARED
    src/plugin.cpp
    src/hooks.cpp
    src/ipc.cpp
    src/state.cpp
    src/actions.cpp
    src/config.cpp
)

target_include_directories(kenshi_ai PRIVATE
    include
    "${KENSHILIB_DIR}/Include"
)

target_link_libraries(kenshi_ai PRIVATE
    "${KENSHILIB_LIB}"
    winhttp         # WinHTTP for local HTTP to sidecar
    ntdll           # needed by KenshiLib on some configs
)

# Export startPlugin with the exact mangled name RE_Kenshi expects
# MSVC mangles void startPlugin() -> ?startPlugin@@YAXXZ
target_compile_definitions(kenshi_ai PRIVATE KENSHI_AI_EXPORTS)

# Match Kenshi's runtime (MD = multithreaded DLL)
set_property(TARGET kenshi_ai PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

# Output directly into the mod folder for easy testing
# Override with:  cmake -DKENSHI_MOD_OUTPUT_DIR="C:/path/to/mods/kenshi-ai" ..
set(KENSHI_MOD_OUTPUT_DIR "" CACHE PATH "Copy DLL here after build")
if(KENSHI_MOD_OUTPUT_DIR)
    add_custom_command(TARGET kenshi_ai POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:kenshi_ai> "${KENSHI_MOD_OUTPUT_DIR}/kenshi_ai.dll"
        COMMENT "Deploying kenshi_ai.dll to mod folder"
    )
endif()
