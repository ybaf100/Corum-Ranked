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

project(CorumRanked VERSION 0.4.0)

option(CORUM_RANKED_DEBUG_BOT_MATCH "Compile the development-only Bot Match harness" OFF)
if (DEFINED ENV{CORUM_RANKED_DEBUG_BOT_MATCH})
    if ("$ENV{CORUM_RANKED_DEBUG_BOT_MATCH}" STREQUAL "1" OR
        "$ENV{CORUM_RANKED_DEBUG_BOT_MATCH}" STREQUAL "ON" OR
        "$ENV{CORUM_RANKED_DEBUG_BOT_MATCH}" STREQUAL "true")
        set(CORUM_RANKED_DEBUG_BOT_MATCH ON)
    endif()
endif()

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
if (NOT CORUM_RANKED_DEBUG_BOT_MATCH)
    list(FILTER SOURCES EXCLUDE REGEX "/debug/.*\\.cpp$")
endif()
add_library(${PROJECT_NAME} SHARED ${SOURCES})
if (CORUM_RANKED_DEBUG_BOT_MATCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE CORUM_RANKED_DEBUG_BOT_MATCH=1)
    message(STATUS "Corum Ranked Debug Bot Match: ENABLED")
else()
    message(STATUS "Corum Ranked Debug Bot Match: DISABLED")
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK. Set GEODE_SDK to the SDK directory.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
