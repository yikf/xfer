#!/usr/bin/env bash
# Format all C/C++ sources in the project using clang-format.
#
# Uses a top-level .clang-format file if present; otherwise applies a
# reasonable default style.  Honours the CLANG_FORMAT_BIN env var so
# a specific version (e.g. clang-format-18) can be selected.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

FORMAT_BIN="${CLANG_FORMAT_BIN:-clang-format}"
if ! command -v "${FORMAT_BIN}" >/dev/null 2>&1; then
    echo "ERROR: ${FORMAT_BIN} not found in PATH. Install clang-format or set CLANG_FORMAT_BIN." >&2
    exit 1
fi

echo ">>> Using ${FORMAT_BIN} ($("${FORMAT_BIN}" --version))"
echo ">>> Formatting sources under ${PROJECT_ROOT}/src ${PROJECT_ROOT}/tests ${PROJECT_ROOT}/include"

cd "${PROJECT_ROOT}"
find src tests include -type f \
    \( -name '*.cpp' -o -name '*.cc' -o -name '*.c' -o -name '*.h' -o -name '*.hpp' \) \
    -print0 | xargs -0 "${FORMAT_BIN}" -i --style=file

echo ">>> Format complete."
