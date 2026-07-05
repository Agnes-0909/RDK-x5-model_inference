#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-${SCRIPT_DIR}/build_aarch64}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
DNN_ROOT="${DNN_ROOT:-${PROJECT_ROOT}/3rdlibrary/DNN}"
OpenCV_DIR="${OpenCV_DIR:-${PROJECT_ROOT}/3rdlibrary/opencv_aarch64/lib/cmake/opencv4}"
RDK_CAMERA_ROOT="${RDK_CAMERA_ROOT:-${PROJECT_ROOT}/3rdlibrary/RDK_CAMERA}"
CMAKE_GENERATOR="${CMAKE_GENERATOR:-Unix Makefiles}"
BUILD_JOBS="${BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"
TOOLCHAIN_FILE="${BUILD_DIR}/aarch64-linux-gnu.toolchain.cmake"
BUILD_TARGET=""
EXTRA_CMAKE_ARGS=()

if [[ -n "${RDK_CAMERA_INCLUDE_DIRS:-}" ]]; then
  EXTRA_CMAKE_ARGS+=("-DRDK_CAMERA_INCLUDE_DIRS=${RDK_CAMERA_INCLUDE_DIRS}")
fi
if [[ -n "${RDK_CAMERA_LIBRARY_DIRS:-}" ]]; then
  EXTRA_CMAKE_ARGS+=("-DRDK_CAMERA_LIBRARY_DIRS=${RDK_CAMERA_LIBRARY_DIRS}")
fi
if [[ -n "${RDK_CAMERA_LIBRARIES:-}" ]]; then
  EXTRA_CMAKE_ARGS+=("-DRDK_CAMERA_LIBRARIES=${RDK_CAMERA_LIBRARIES}")
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    --target)
      BUILD_TARGET="$2"
      if [[ "${BUILD_TARGET}" == "test_mipi_zero_copy_config" ]]; then
        EXTRA_CMAKE_ARGS+=("-DBUILD_MIPI_CAMERA_APP=OFF")
      fi
      shift 2
      ;;
    *)
      break
      ;;
  esac
done

if command -v cmake >/dev/null 2>&1; then
  CMAKE_BIN="$(command -v cmake)"
elif command -v cmake3 >/dev/null 2>&1; then
  CMAKE_BIN="$(command -v cmake3)"
else
  echo "Error: neither 'cmake' nor 'cmake3' was found in PATH." >&2
  exit 127
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"

cat > "${TOOLCHAIN_FILE}" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)
set(CMAKE_AR aarch64-linux-gnu-ar)
set(CMAKE_RANLIB aarch64-linux-gnu-ranlib)
set(CMAKE_STRIP aarch64-linux-gnu-strip)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
EOF

"${CMAKE_BIN}" \
  -S "${SCRIPT_DIR}" \
  -B "${BUILD_DIR}" \
  -G "${CMAKE_GENERATOR}" \
  -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DDNN_ROOT="${DNN_ROOT}" \
  -DOpenCV_DIR="${OpenCV_DIR}" \
  -DRDK_CAMERA_ROOT="${RDK_CAMERA_ROOT}" \
  "${EXTRA_CMAKE_ARGS[@]}" \
  "$@"

if [[ -n "${BUILD_TARGET}" ]]; then
  "${CMAKE_BIN}" --build "${BUILD_DIR}" --target "${BUILD_TARGET}" --parallel "${BUILD_JOBS}"
else
  "${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel "${BUILD_JOBS}"
fi
