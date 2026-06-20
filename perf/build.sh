#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

DOCKER_CONTAINER="${DOCKER_CONTAINER:-3c543521b4ca}"
DOCKER_PROJECT_DIR="${DOCKER_PROJECT_DIR:-/project}"
BUILD_SCRIPT_IN_CONTAINER="${BUILD_SCRIPT_IN_CONTAINER:-multi_thread_proj/build.sh}"
DOCKER_SHELL="${DOCKER_SHELL:-bash}"

print_usage() {
  cat <<EOF
Usage: $(basename "$0") [--container <id-or-name>] [extra build args...]

Host-side wrapper that runs the project build inside a Docker container.

Environment overrides:
  DOCKER_CONTAINER           Container ID or name (default: ${DOCKER_CONTAINER})
  DOCKER_PROJECT_DIR         Project mount path inside container (default: ${DOCKER_PROJECT_DIR})
  BUILD_SCRIPT_IN_CONTAINER  Build script path relative to project root inside container
                             (default: ${BUILD_SCRIPT_IN_CONTAINER})
  DOCKER_SHELL               Shell used by docker exec (default: ${DOCKER_SHELL})

Examples:
  $0
  $0 --container 3c543521b4ca
  BUILD_SCRIPT_IN_CONTAINER=model_infer/build.sh $0
  BUILD_TYPE=Debug $0 -- --target mt_server
EOF
}

BUILD_ARGS=()
while (($# > 0)); do
  case "$1" in
    --container)
      if (($# < 2)); then
        echo "Error: --container requires a value." >&2
        exit 2
      fi
      DOCKER_CONTAINER="$2"
      shift 2
      ;;
    --help|-h)
      print_usage
      exit 0
      ;;
    --)
      shift
      BUILD_ARGS+=("$@")
      break
      ;;
    *)
      BUILD_ARGS+=("$1")
      shift
      ;;
  esac
done

if ! command -v docker >/dev/null 2>&1; then
  echo "Error: docker not found in PATH." >&2
  exit 127
fi

if ! docker inspect "${DOCKER_CONTAINER}" >/dev/null 2>&1; then
  echo "Error: container '${DOCKER_CONTAINER}' does not exist." >&2
  exit 1
fi

CONTAINER_STATE="$(docker inspect --format '{{.State.Status}}' "${DOCKER_CONTAINER}")"
if [[ "${CONTAINER_STATE}" != "running" ]]; then
  echo "Error: container '${DOCKER_CONTAINER}' is not running (state=${CONTAINER_STATE})." >&2
  exit 1
fi

MOUNT_SOURCE="$(docker inspect --format '{{range .Mounts}}{{if eq .Destination "'"${DOCKER_PROJECT_DIR}"'"}}{{.Source}}{{end}}{{end}}' "${DOCKER_CONTAINER}")"
if [[ -z "${MOUNT_SOURCE}" ]]; then
  echo "Error: container '${DOCKER_CONTAINER}' does not mount '${DOCKER_PROJECT_DIR}'." >&2
  exit 1
fi

EXPECTED_SOURCE="$(cd "${PROJECT_ROOT}" && pwd)"
if [[ "${MOUNT_SOURCE}" != "${EXPECTED_SOURCE}" ]]; then
  echo "Error: '${DOCKER_PROJECT_DIR}' in container '${DOCKER_CONTAINER}' maps to '${MOUNT_SOURCE}', expected '${EXPECTED_SOURCE}'." >&2
  exit 1
fi

BUILD_SCRIPT_ABS="${DOCKER_PROJECT_DIR}/${BUILD_SCRIPT_IN_CONTAINER}"

FORWARDED_ARGS=""
if ((${#BUILD_ARGS[@]} > 0)); then
  printf -v FORWARDED_ARGS ' %q' "${BUILD_ARGS[@]}"
fi

printf -v BUILD_SCRIPT_ABS_Q '%q' "${BUILD_SCRIPT_ABS}"
printf -v DOCKER_PROJECT_DIR_Q '%q' "${DOCKER_PROJECT_DIR}"

echo "Docker container: ${DOCKER_CONTAINER}"
echo "Project mount:    ${DOCKER_PROJECT_DIR}"
echo "Build script:     ${BUILD_SCRIPT_ABS}"

docker exec \
  -e BUILD_DIR="${BUILD_DIR:-}" \
  -e BUILD_TYPE="${BUILD_TYPE:-}" \
  -e DNN_ROOT="${DNN_ROOT:-}" \
  -e OpenCV_DIR="${OpenCV_DIR:-}" \
  -e CMAKE_GENERATOR="${CMAKE_GENERATOR:-}" \
  -e BUILD_JOBS="${BUILD_JOBS:-}" \
  "${DOCKER_CONTAINER}" \
  "${DOCKER_SHELL}" -lc "
    set -euo pipefail
    if [ ! -f ${BUILD_SCRIPT_ABS_Q} ]; then
      echo 'Error: build script not found: ${BUILD_SCRIPT_ABS}' >&2
      exit 1
    fi
    chmod +x ${BUILD_SCRIPT_ABS_Q}
    cd ${DOCKER_PROJECT_DIR_Q}
    ${BUILD_SCRIPT_ABS_Q}${FORWARDED_ARGS}
  "
