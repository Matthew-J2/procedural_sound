cmake_minimum_required(VERSION 3.20)

project(MyProject LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# -------------------------------------------------------
# C++ standard
# -------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------------------------------------
# Testing
# -------------------------------------------------------
enable_testing()
include(CTest)

# -------------------------------------------------------
# FetchContent
# -------------------------------------------------------
include(FetchContent)

# -------------------------------------------------------
# Miniaudio
# -------------------------------------------------------
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)
set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(miniaudio)

# -------------------------------------------------------
# Core library
# -------------------------------------------------------
add_library(core
    src/core.cpp
    src/instrument.cpp
)

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_options(core PRIVATE
    -Wall
    -Wextra
    -Wpedantic
)

target_link_libraries(core PUBLIC
    miniaudio
)

# -------------------------------------------------------
# Main executable
# -------------------------------------------------------
add_executable(generator
    src/main.cpp
)

target_link_libraries(generator PRIVATE
    core
)

# -------------------------------------------------------
# GoogleTest
# -------------------------------------------------------
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

FetchContent_MakeAvailable(googletest)

# -------------------------------------------------------
# Tests
# -------------------------------------------------------
add_executable(unit_tests
    tests/test_main.cpp
    tests/test_oscillator.cpp
    tests/test_core.cpp
    tests/test_graph.cpp
)

target_link_libraries(unit_tests PRIVATE
    core
    GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(unit_tests)
