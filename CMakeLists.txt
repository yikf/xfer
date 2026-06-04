cmake_minimum_required(VERSION 3.16)

# ---- Project metadata ----
project(
    xfer
    VERSION 0.1.0
    DESCRIPTION "High-performance intranet file transfer tool"
    HOMEPAGE_URL "https://example.com/xfer"
    LANGUAGES CXX
)

# ---- Global C++ settings ----
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate compile_commands.json for clangd / IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Build options ----
option(XFER_BUILD_TESTS "Build the unit tests" ON)
option(XFER_ENABLE_ASAN "Enable AddressSanitizer in debug builds" ON)
option(XFER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# ---- Compiler flags ----
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wunused -Wunreachable-code -Wmissing-declarations
        -Wnon-virtual-dtor -Wold-style-cast -Wcast-align
    )
    if(XFER_WARNINGS_AS_ERRORS)
        add_compile_options(-Werror)
    endif()

    # Debug: AddressSanitizer + UndefinedBehaviourSanitizer (Linux/macOS)
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

# ---- Output paths ----
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ---- Dependencies ----
# Reserved for future: spdlog / gflags / googletest / boost.asio / OpenSSL ...
# find_package(Threads REQUIRED)

# ---- Subdirectories ----
add_subdirectory(src)

if(XFER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---- Summary ----
message(STATUS "")
message(STATUS "===== xfer ${PROJECT_VERSION} =====")
message(STATUS "  Build type     : ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ std        : ${CMAKE_CXX_STANDARD}")
message(STATUS "  Compiler       : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Build tests    : ${XFER_BUILD_TESTS}")
message(STATUS "  ASan (Debug)   : ${XFER_ENABLE_ASAN}")
message(STATUS "  Warnings -> Err: ${XFER_WARNINGS_AS_ERRORS}")
message(STATUS "")
