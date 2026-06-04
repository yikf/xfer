#!/usr/bin/env bash
# Remove build artifacts produced by scripts/build.sh / cmake.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo ">>> Removing ${PROJECT_ROOT}/build"
rm -rf "${PROJECT_ROOT}/build"
echo ">>> Removing ${PROJECT_ROOT}/cmake-build-debug"
rm -rf "${PROJECT_ROOT}/cmake-build-debug"
echo ">>> Clean complete."
