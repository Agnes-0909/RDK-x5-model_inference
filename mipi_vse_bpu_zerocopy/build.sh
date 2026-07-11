#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build_aarch64}"
TOOLCHAIN_FILE="${BUILD_DIR}/aarch64-linux-gnu.toolchain.cmake"
BUILD_TYPE="${BUILD_TYPE:-Release}"
BUILD_JOBS="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
BUILD_TARGET=""

if [[ "${1:-}" == "--target" ]]; then
  BUILD_TARGET="${2:?--target requires a CMake target}"
  shift 2
fi
if [[ $# -ne 0 ]]; then
  echo "Unknown arguments: $*" >&2
  exit 2
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cat >"${TOOLCHAIN_FILE}" <<'EOF'
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DDNN_ROOT="${PROJECT_ROOT}/3rdlibrary/DNN" \
  -DRDK_CAMERA_ROOT="${PROJECT_ROOT}/3rdlibrary/RDK_CAMERA"

if [[ -n "${BUILD_TARGET}" ]]; then
  cmake --build "${BUILD_DIR}" --target "${BUILD_TARGET}" -j"${BUILD_JOBS}"
else
  cmake --build "${BUILD_DIR}" -j"${BUILD_JOBS}"
fi
