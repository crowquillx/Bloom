cmake_minimum_required(VERSION 3.16)

project(Bloom VERSION 0.5.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

set(BLOOM_BUILD_CHANNEL "stable" CACHE STRING "Bloom build channel metadata")
set(BLOOM_BUILD_ID "${PROJECT_VERSION}" CACHE STRING "Bloom build identifier metadata")
set(BLOOM_GIT_SHA "$ENV{GITHUB_SHA}" CACHE STRING "Bloom Git SHA metadata")
set(BLOOM_UPDATE_MANIFEST_BASE_URL "https://raw.githubusercontent.com/crowquillx/Bloom/update-manifests" CACHE STRING "Base URL for update manifests")
set(BLOOM_ALLOWED_BUILD_CHANNELS stable dev)
if(NOT BLOOM_BUILD_CHANNEL IN_LIST BLOOM_ALLOWED_BUILD_CHANNELS)
    message(FATAL_ERROR "Invalid BLOOM_BUILD_CHANNEL '${BLOOM_BUILD_CHANNEL}'. Allowed values: ${BLOOM_ALLOWED_BUILD_CHANNELS}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 Network Sql Concurrent Multimedia Test ShaderTools LinguistTools)
find_package(Qt6 COMPONENTS MultimediaQuick QUIET)
include(CTest)

# Platform-specific secure storage dependencies
if(UNIX AND NOT APPLE)
    # Linux: use pkg-config to find libsecret-1
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED libsecret-1)
endif()


add_subdirectory(src)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
