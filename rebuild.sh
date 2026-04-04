#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

echo "[RetroWave] Removing CMake cache and build artifacts..."
rm -rf "${BUILD_DIR}"

echo "[RetroWave] Configuring project..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"

echo "[RetroWave] Building..."
cmake --build "${BUILD_DIR}" "$@"

echo "[RetroWave] Done: ${BUILD_DIR}/retrowave"
