# =============================================================================
# xfer core library + CLI executable.
#
# xfer_core is a static library containing networking, protocol, transfer,
# and common utility code.  It is intentionally header-light in its public
# surface (see include/xfer/), so external consumers only need to link against
# it without pulling in internal details.
#
# xfer is the command-line driver.  Keep its sources (main.cpp, cli.cpp) free
# of complex business logic -- all transfer semantics live in xfer_core.
# =============================================================================

set(XFER_CORE_SOURCES
    common/crc32.cpp
    common/format.cpp
    common/logging.cpp
    common/progress.cpp
    common/stopwatch.cpp
    net/socket.cpp
    transfer/protocol.cpp
    transfer/sender.cpp
    transfer/receiver.cpp
)

add_library(xfer_core STATIC ${XFER_CORE_SOURCES})

target_include_directories(
    xfer_core
    PUBLIC  ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${PROJECT_SOURCE_DIR}/src
)
target_link_libraries(xfer_core PUBLIC Threads::Threads)

add_executable(xfer main.cpp cli.cpp)
target_include_directories(xfer PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xfer PRIVATE xfer_core)
