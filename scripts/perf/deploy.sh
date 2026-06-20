#!/usr/bin/env bash

set -euo pipefail

SOURCE_PATH="${BASH_SOURCE[0]}"
while [[ -L "${SOURCE_PATH}" ]]; do
  SOURCE_DIR="$(cd "$(dirname "${SOURCE_PATH}")" && pwd)"
  SOURCE_PATH="$(readlink "${SOURCE_PATH}")"
  [[ "${SOURCE_PATH}" != /* ]] && SOURCE_PATH="${SOURCE_DIR}/${SOURCE_PATH}"
done

SCRIPT_DIR="$(cd "$(dirname "${SOURCE_PATH}")" && pwd)"
# shellcheck source=./env.sh
source "${SCRIPT_DIR}/env.sh"

print_usage() {
  cat <<EOF
Usage: $(basename "$0")

Deploy mt_server and mt_manager binaries to the board and prepare runtime paths.

Environment overrides:
  BOARD_HOST              default: ${BOARD_HOST}
  BOARD_BASE_DIR          default: ${BOARD_BASE_DIR}
  BOARD_BIN_DIR           default: ${BOARD_BIN_DIR}
  BOARD_LIB_DIR           default: ${BOARD_LIB_DIR}
  BOARD_MODEL_PATH        default: ${BOARD_MODEL_PATH}
  BOARD_INPUT_DIR_0       default: ${BOARD_INPUT_DIR_0}
  BOARD_INPUT_DIR_1       default: ${BOARD_INPUT_DIR_1}
  BOARD_INPUT_DIR_2       default: ${BOARD_INPUT_DIR_2}
  BOARD_LOG_DIR           default: ${BOARD_LOG_DIR}
  BOARD_UDS_PATH          default: ${BOARD_UDS_PATH}
  BOARD_PASSWORD          default: ${BOARD_PASSWORD}
  SERVER_BINARY_LOCAL     default: ${SERVER_BINARY_LOCAL}
  MANAGER_BINARY_LOCAL    default: ${MANAGER_BINARY_LOCAL}
  SCP_BIN                 default: ${SCP_BIN}
  SSH_BIN                 default: ${SSH_BIN}
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

for local_file in "${SERVER_BINARY_LOCAL}" "${MANAGER_BINARY_LOCAL}"; do
  if [[ ! -f "${local_file}" ]]; then
    echo "Error: local binary not found: ${local_file}" >&2
    exit 1
  fi
done

if ! command -v "${SCP_BIN}" >/dev/null 2>&1; then
  echo "Error: scp command not found: ${SCP_BIN}" >&2
  exit 127
fi

if ! command -v "${SSH_BIN}" >/dev/null 2>&1; then
  echo "Error: ssh command not found: ${SSH_BIN}" >&2
  exit 127
fi

SSH_ARGS=("${SSH_BIN}")
SCP_ARGS=("${SCP_BIN}")
if [[ -n "${BOARD_PASSWORD}" ]]; then
  if ! command -v sshpass >/dev/null 2>&1; then
    echo "Error: BOARD_PASSWORD is set but sshpass is not installed." >&2
    exit 127
  fi
  SSH_ARGS=(sshpass -p "${BOARD_PASSWORD}" "${SSH_BIN}" -o StrictHostKeyChecking=no)
  SCP_ARGS=(sshpass -p "${BOARD_PASSWORD}" "${SCP_BIN}" -o StrictHostKeyChecking=no)
fi

REMOTE_SETUP_SCRIPT=$(cat <<EOF
set -euo pipefail
mkdir -p '${BOARD_BIN_DIR}' '${BOARD_LOG_DIR}' \
         '${BOARD_LIB_DIR}' \
         '${BOARD_INPUT_DIR_0}' '${BOARD_INPUT_DIR_1}' '${BOARD_INPUT_DIR_2}' \
         '$(dirname "${BOARD_MODEL_PATH}")'
pkill -x '${SERVER_PROCESS_NAME}' 2>/dev/null || true
pkill -x '${MANAGER_PROCESS_NAME}' 2>/dev/null || true
rm -f '${BOARD_UDS_PATH}'
rm -f '/dev/shm/$(basename "${BOARD_SHM_PATH}")'
rm -f '${REMOTE_SERVER_LOG}' '${REMOTE_MANAGER_LOG}'
EOF
)

echo "Preparing board directories and cleaning old processes on ${BOARD_HOST}"
"${SSH_ARGS[@]}" "${BOARD_HOST}" "${REMOTE_SETUP_SCRIPT}"

echo "Copying binaries to board"
SERVER_BINARY_REMOTE_TMP="${SERVER_BINARY_REMOTE}.tmp"
MANAGER_BINARY_REMOTE_TMP="${MANAGER_BINARY_REMOTE}.tmp"
"${SCP_ARGS[@]}" "${SERVER_BINARY_LOCAL}" "${BOARD_HOST}:${SERVER_BINARY_REMOTE_TMP}"
"${SCP_ARGS[@]}" "${MANAGER_BINARY_LOCAL}" "${BOARD_HOST}:${MANAGER_BINARY_REMOTE_TMP}"

REMOTE_INSTALL_SCRIPT=$(cat <<EOF
set -euo pipefail
mv -f '${SERVER_BINARY_REMOTE_TMP}' '${SERVER_BINARY_REMOTE}'
mv -f '${MANAGER_BINARY_REMOTE_TMP}' '${MANAGER_BINARY_REMOTE}'
chmod +x '${SERVER_BINARY_REMOTE}' '${MANAGER_BINARY_REMOTE}'
EOF
)

"${SSH_ARGS[@]}" "${BOARD_HOST}" "${REMOTE_INSTALL_SCRIPT}"

echo "Deploy complete."
echo "Remote server binary: ${SERVER_BINARY_REMOTE}"
echo "Remote manager binary: ${MANAGER_BINARY_REMOTE}"
