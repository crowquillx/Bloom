cmake_minimum_required(VERSION 3.16)

project(Bloom VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

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
