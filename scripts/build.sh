#!/usr/bin/env bash
# Build the xfer project.
#
# Usage: ./scripts/build.sh [Debug|Release] [--clean]
#
# Environment variables:
#   BUILD_TYPE   - CMake build type (default: Release)
#   JOBS         - Parallelism for the build step (default: auto-detect)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="${1:-Release}"
case "$BUILD_TYPE" in
    Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    --clean) BUILD_TYPE="Release" ;;
    *) echo "Unknown build type: $BUILD_TYPE" >&2; exit 1 ;;
esac

# Optional second/third argument: --clean flag.
CLEAN=0
for arg in "$@"; do
    if [ "$arg" = "--clean" ]; then CLEAN=1; fi
done

BUILD_DIR="${PROJECT_ROOT}/build"

if [ "$CLEAN" -eq 1 ]; then
    echo ">>> Cleaning build directory: ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

echo ">>> Configuring (${BUILD_TYPE})"
cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}" -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

: "${JOBS:=$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 2)}"
echo ">>> Building with ${JOBS} parallel jobs"
cmake --build "${BUILD_DIR}" -j "${JOBS}"

echo ">>> Build complete. Binaries in ${BUILD_DIR}/bin/"
ls -la "${BUILD_DIR}/bin/" 2>/dev/null || true
