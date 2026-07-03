cmake_minimum_required(VERSION 3.16)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BLOOM_PROJECT_VERSION LIMIT_COUNT 1)
project(Bloom VERSION ${BLOOM_PROJECT_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

set(BLOOM_BUILD_CHANNEL "stable" CACHE STRING "Bloom build channel metadata")
set(BLOOM_BUILD_ID "${PROJECT_VERSION}" CACHE STRING "Bloom build identifier metadata")
set(BLOOM_GIT_SHA "$ENV{GITHUB_SHA}" CACHE STRING "Bloom Git SHA metadata")
set(BLOOM_UPDATE_MANIFEST_BASE_URL "https://raw.githubusercontent.com/crowquillx/Bloom/update-manifests" CACHE STRING "Base URL for update manifests")
option(BLOOM_BUNDLE_LIBMPV "Install the discovered libmpv runtime beside Bloom" ON)
option(BLOOM_BUILD_VISUAL_TESTS "Build the environment-sensitive visual regression test" ON)
set(BLOOM_ALLOWED_BUILD_CHANNELS stable dev)
if(NOT BLOOM_BUILD_CHANNEL IN_LIST BLOOM_ALLOWED_BUILD_CHANNELS)
    message(FATAL_ERROR "Invalid BLOOM_BUILD_CHANNEL '${BLOOM_BUILD_CHANNEL}'. Allowed values: ${BLOOM_ALLOWED_BUILD_CHANNELS}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Sql Concurrent Multimedia Test ShaderTools LinguistTools)
find_package(Qt6 COMPONENTS MultimediaQuick QUIET)
include(CTest)
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        # Fall back to a pinned FetchContent build for platforms without a
        # system GoogleTest (e.g. Windows CI). Keep this hardcoded commit in
        # sync with the gtest commit in packaging/dependencies.json.
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
            GIT_SHALLOW TRUE
        )
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
        if(TARGET gtest AND NOT TARGET GTest::gtest)
            add_library(GTest::gtest ALIAS gtest)
            add_library(GTest::gtest_main ALIAS gtest_main)
        endif()
    endif()
endif()

# Platform-specific secure storage dependencies
if(UNIX AND NOT APPLE)
    # Linux: use pkg-config to find libsecret-1
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
    pkg_check_modules(SDL2 QUIET sdl2)
endif()

if(WIN32)
    find_package(SDL2 CONFIG QUIET)
    if(NOT SDL2_FOUND)
        find_path(SDL2_INCLUDE_DIR
            NAMES SDL.h
            HINTS
                $ENV{SDL2_ROOT}
                $ENV{SDL2_DIR}
            PATH_SUFFIXES
                include
                include/SDL2
        )
        find_library(SDL2_LIBRARY
            NAMES SDL2 SDL2-static
            HINTS
                $ENV{SDL2_ROOT}
                $ENV{SDL2_DIR}
            PATH_SUFFIXES
                lib
                lib/x64
                lib/x86
                lib/cmake/SDL2
        )
        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            set(SDL2_FOUND TRUE)
            set(SDL2_INCLUDE_DIRS "${SDL2_INCLUDE_DIR}")
            set(SDL2_LIBRARIES "${SDL2_LIBRARY}")
        endif()
    endif()
endif()


add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
