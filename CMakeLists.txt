# =============================================================================
# Top-level build configuration for the xfer project.
#
# Usage:
#     cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#     cmake --build build -j
#     ctest --test-dir build --output-on-failure
#
# Key options:
#     -DCMAKE_BUILD_TYPE        = Debug | Release | RelWithDebInfo | MinSizeRel
#     -DXFER_BUILD_TESTS        = ON | OFF
#     -DXFER_ENABLE_ASAN        = ON | OFF   (used only in Debug builds)
#     -DXFER_WARNINGS_AS_ERRORS = ON | OFF
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(
    xfer
    VERSION 0.1.0
    DESCRIPTION "High-performance intranet file transfer tool"
    HOMEPAGE_URL "https://github.com/yikf/xfer"
    LANGUAGES CXX
)

# ---- Global C++ settings ----------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Build options (cache variables) ----------------------------------------
option(XFER_BUILD_TESTS "Build the unit tests" ON)
option(XFER_ENABLE_ASAN "Enable AddressSanitizer + UBSan in Debug builds" ON)
option(XFER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ---- Compiler flags ---------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wunused -Wunreachable-code -Wmissing-declarations
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    )
    if(XFER_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()

    if(XFER_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
    if(XFER_WARNINGS_AS_ERRORS)
        add_compile_options(/WX)
    endif()
endif()

# ---- Output layout ----------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Threads (needed by the test framework) ---------------------------------
find_package(Threads REQUIRED)

# ---- Subdirectories ---------------------------------------------------------
add_subdirectory(src)

if(XFER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Summary ----------------------------------------------------------------
message(STATUS "")
message(STATUS "===== xfer ${PROJECT_VERSION} =====")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler       : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build tests    : ${XFER_BUILD_TESTS}")
message(STATUS "  ASan (Debug)   : ${XFER_ENABLE_ASAN}")
message(STATUS "  Warnings -> Err: ${XFER_WARNINGS_AS_ERRORS}")
message(STATUS "")
