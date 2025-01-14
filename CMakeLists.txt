cmake_minimum_required(VERSION 3.16)

option(MTR_ENABLED "Enable minitrace profiling" OFF)
option(BUILD_TESTS "Build tests" OFF)

set(CXX_STANDARD_REQUIRED true)
set(CMAKE_CXX_STANDARD 11)

project(
    p4-fusion
    LANGUAGES CXX
    VERSION 1.0
    DESCRIPTION "Custom tool to convert P4 depots to Git repositories using the Helix Core C++ API"
)

set(BUILD_CLAR OFF)
set(BUILD_SHARED_LIBS OFF)

set(HELIX_API vendor/helix-core-api/$<IF:$<PLATFORM_ID:Darwin>,mac,linux>)


if (NOT OPENSSL_ROOT_DIR)
    set(OPENSSL_ROOT_DIR /usr/local/ssl)
endif()
find_package(OpenSSL)

find_package(PkgConfig)
pkg_check_modules(LIBGIT2 libgit2)
if(NOT LIBGIT2_FOUND)
    message("libgit2 not found, compile own version")
    add_subdirectory(vendor/libgit2)
endif()

if(MTR_ENABLED)
    add_subdirectory(vendor/minitrace)
endif()

add_subdirectory(p4-fusion)

if (BUILD_TESTS)
    message(STATUS "Building tests")
    add_subdirectory(tests)
endif()
