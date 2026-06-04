# ---- unit test helper (linked into each individual target below) ----
add_library(xfer_test_common INTERFACE)
target_include_directories(xfer_test_common INTERFACE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xfer_test_common INTERFACE xfer_core)

# ---- smoke test: version string ----
add_executable(xfer_test_basic test_basic.cpp)
target_link_libraries(xfer_test_basic PRIVATE xfer_test_common)
add_test(NAME xfer_test_basic COMMAND xfer_test_basic)

# ---- full roundtrip over localhost socket pair (send → recv, crc verify) ----
add_executable(xfer_test_roundtrip test_roundtrip.cpp)
target_link_libraries(xfer_test_roundtrip PRIVATE xfer_test_common)
add_test(NAME xfer_test_roundtrip COMMAND xfer_test_roundtrip)
