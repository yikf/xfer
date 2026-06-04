#!/usr/bin/env bash
# Build the project and run the full test suite.
#
# Usage: ./scripts/test.sh [Debug|Release] [test-name-filter]
#
# The first non-keyword argument is the build type (default: Debug, since
# that enables AddressSanitizer + UBSan).  Any subsequent tokens are joined
# into a gtest filter and forwarded to ctest.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_TYPE="Debug"
FILTER=""
for arg in "$@"; do
    case "$arg" in
        Debug|Release|RelWithDebInfo|MinSizeRel) BUILD_TYPE="$arg" ;;
        *) FILTER="${FILTER:+${FILTER} }${arg}" ;;
    esac
done

echo ">>> Building in ${BUILD_TYPE}"
"${PROJECT_ROOT}/scripts/build.sh" "${BUILD_TYPE}"

BUILD_DIR="${PROJECT_ROOT}/build"
echo ">>> Running tests (filter: '${FILTER:-<all>}')"
if [ -n "$FILTER" ]; then
    ctest --test-dir "${BUILD_DIR}" --output-on-failure --verbose -R "${FILTER}"
else
    ctest --test-dir "${BUILD_DIR}" --output-on-failure
fi
echo ">>> Tests passed."
