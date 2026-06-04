# ---- Shared core library ----
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

# ---- Executable ----
add_executable(xfer main.cpp cli.cpp)
target_include_directories(xfer PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(xfer PRIVATE xfer_core)
