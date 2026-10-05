cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if (APPLE)
    if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" OR IOS)
        set(CMAKE_OSX_ARCHITECTURES "arm64")
    else()
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
    endif()
endif()

project(CorumRanked VERSION 0.5.0)

option(
    CORUM_RANKED_DEBUG_BOT_MATCH
    "Include the temporary development-only Debug Bot Match UI and client route"
    OFF
)
option(CORUM_RANKED_DEBUG_SPECTATE "Include server-backed VOICE Debug Spectate harness" OFF)

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
# Debug Bot Match, including Force Win/Lose, is a development tool and must not
# be linked into official packages. Add its implementation files only when the
# opt-in development build flag is enabled.
list(FILTER SOURCES EXCLUDE REGEX ".*/src/DebugBotPopup\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/debug/DebugBotConfig\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/debug/DebugBotPopup\\.cpp$")
list(FILTER SOURCES EXCLUDE REGEX ".*/src/debug/DebugSpectate.*\\.cpp$")
if (CORUM_RANKED_DEBUG_BOT_MATCH)
    list(APPEND SOURCES
        src/DebugBotPopup.cpp
        src/debug/DebugBotConfig.cpp
    )
endif()
if (CORUM_RANKED_DEBUG_SPECTATE)
    list(APPEND SOURCES src/debug/DebugSpectateRuntime.cpp src/debug/DebugSpectatePopup.cpp)
endif()
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (CORUM_RANKED_DEBUG_BOT_MATCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CORUM_RANKED_DEBUG_BOT_MATCH=1)
endif()
if (CORUM_RANKED_DEBUG_SPECTATE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CORUM_RANKED_DEBUG_SPECTATE=1)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK. Set GEODE_SDK to the SDK directory.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
