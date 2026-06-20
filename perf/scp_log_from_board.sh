#!/usr/bin/env bash

set -euo pipefail

SOURCE_PATH="${BASH_SOURCE[0]}"
while [[ -L "${SOURCE_PATH}" ]]; do
  SOURCE_DIR="$(cd "$(dirname "${SOURCE_PATH}")" && pwd)"
  SOURCE_PATH="$(readlink "${SOURCE_PATH}")"
  [[ "${SOURCE_PATH}" != /* ]] && SOURCE_PATH="${SOURCE_DIR}/${SOURCE_PATH}"
done
SCRIPT_DIR="$(cd "$(dirname "${SOURCE_PATH}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BOARD_HOST="${BOARD_HOST:-root@192.168.1.12}"
BOARD_BASE_DIR="${BOARD_BASE_DIR:-/root/multi_thread_proj}"
BOARD_LOG_DIR="${BOARD_LOG_DIR:-${BOARD_BASE_DIR}/log}"
BOARD_PASSWORD="${BOARD_PASSWORD:-root}"
SCP_BIN="${SCP_BIN:-scp}"
LOCAL_LOG_DIR="${LOCAL_LOG_DIR:-${PROJECT_ROOT}/skills_tmp_log}"
REMOTE_SERVER_LOG="${BOARD_LOG_DIR}/mt_server.log"
REMOTE_MANAGER_LOG="${BOARD_LOG_DIR}/mt_manager.log"

print_usage() {
  cat <<EOF
Usage: $(basename "$0")

Copy mt_server.log and mt_manager.log from board to local directory.
The local destination is cleared before copying.

Environment overrides:
  BOARD_HOST      default: ${BOARD_HOST}
  BOARD_BASE_DIR  default: ${BOARD_BASE_DIR}
  BOARD_LOG_DIR   default: ${BOARD_LOG_DIR}
  BOARD_PASSWORD  default: ${BOARD_PASSWORD}
  SCP_BIN         default: ${SCP_BIN}
  LOCAL_LOG_DIR   default: ${LOCAL_LOG_DIR}
EOF
}

if (($# > 0)); then
  case "$1" in
    --help|-h)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      print_usage >&2
      exit 2
      ;;
  esac
fi

if ! command -v "${SCP_BIN}" >/dev/null 2>&1; then
  echo "Error: scp command not found: ${SCP_BIN}" >&2
  exit 127
fi

SCP_ARGS=("${SCP_BIN}")
if [[ -n "${BOARD_PASSWORD}" ]]; then
  if ! command -v sshpass >/dev/null 2>&1; then
    echo "Error: BOARD_PASSWORD is set but sshpass is not installed." >&2
    exit 127
  fi
  SCP_ARGS=(sshpass -p "${BOARD_PASSWORD}" "${SCP_BIN}" -o StrictHostKeyChecking=no)
fi

mkdir -p "${LOCAL_LOG_DIR}"
find "${LOCAL_LOG_DIR}" -mindepth 1 -maxdepth 1 -exec rm -rf {} +

echo "Copying board logs from ${BOARD_HOST}:${BOARD_LOG_DIR} to ${LOCAL_LOG_DIR}"
"${SCP_ARGS[@]}" "${BOARD_HOST}:${REMOTE_SERVER_LOG}" "${LOCAL_LOG_DIR}/mt_server.log"
"${SCP_ARGS[@]}" "${BOARD_HOST}:${REMOTE_MANAGER_LOG}" "${LOCAL_LOG_DIR}/mt_manager.log"

cp "${LOCAL_LOG_DIR}/mt_server.log" "${LOCAL_LOG_DIR}/mt_server_combined.log"

echo "Done."
