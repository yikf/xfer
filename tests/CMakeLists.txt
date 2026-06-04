# =============================================================================
# xfer unit tests using Google Test.
#
# Google Test is fetched via CMake's FetchContent at configure time so that
# we are independent of the host system's package manager.  If for some reason
# the network is unavailable, users can point GTEST_ROOT at a pre-built copy.
#
# Each test target below registers with CTest -- run `ctest --test-dir build`
# (or `./scripts/test.sh`) to execute the entire suite.
# =============================================================================

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2
    GIT_SHALLOW    TRUE
)
# Prevent Google Test from overriding our compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Shared test fixture target: all xfer test binaries link against this.
add_library(xfer_test_common INTERFACE)
target_include_directories(xfer_test_common INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xfer_test_common INTERFACE xfer_core GTest::gtest GTest::gtest_main)

# -----------------------------------------------------------------------------
# Individual test targets.
# -----------------------------------------------------------------------------

add_executable(xfer_test_crc32     test_crc32.cpp)
target_link_libraries(xfer_test_crc32 PRIVATE xfer_test_common)
add_test(NAME xfer_test_crc32 COMMAND xfer_test_crc32)

add_executable(xfer_test_format    test_format.cpp)
target_link_libraries(xfer_test_format PRIVATE xfer_test_common)
add_test(NAME xfer_test_format COMMAND xfer_test_format)

add_executable(xfer_test_protocol  test_protocol.cpp)
target_link_libraries(xfer_test_protocol PRIVATE xfer_test_common)
add_test(NAME xfer_test_protocol COMMAND xfer_test_protocol)

add_executable(xfer_test_version   test_version.cpp)
target_link_libraries(xfer_test_version PRIVATE xfer_test_common)
add_test(NAME xfer_test_version COMMAND xfer_test_version)

add_executable(xfer_test_roundtrip test_roundtrip.cpp)
target_link_libraries(xfer_test_roundtrip PRIVATE xfer_test_common)
add_test(NAME xfer_test_roundtrip COMMAND xfer_test_roundtrip)

# Let IDEs know we want tests included in the default build.
set_target_properties(
    xfer_test_crc32 xfer_test_format xfer_test_protocol
    xfer_test_version xfer_test_roundtrip
    PROPERTIES FOLDER "Tests"
)
