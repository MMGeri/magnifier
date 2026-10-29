#!/usr/bin/env bash
set -euo pipefail

# Cross-platform-ish CMake build script.
# On Linux/macOS this builds the stub executable (runtime is Windows-only).

BUILD_DIR=${BUILD_DIR:-build}
BUILD_TYPE=${BUILD_TYPE:-Release}

cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"
