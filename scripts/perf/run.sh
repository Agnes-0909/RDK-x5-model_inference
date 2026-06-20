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

Run mt_server and mt_manager on the board, wait RUN_SECONDS, then stop them.
Assumes binaries are already deployed.

Environment overrides:
  BOARD_HOST              default: ${BOARD_HOST}
  BOARD_BIN_DIR           default: ${BOARD_BIN_DIR}
  BOARD_LIB_DIR           default: ${BOARD_LIB_DIR}
  BOARD_MODEL_PATH        default: ${BOARD_MODEL_PATH}
  BOARD_INPUT_DIR_0       default: ${BOARD_INPUT_DIR_0}
  BOARD_INPUT_DIR_1       default: ${BOARD_INPUT_DIR_1}
  BOARD_INPUT_DIR_2       default: ${BOARD_INPUT_DIR_2}
  BOARD_LOG_DIR           default: ${BOARD_LOG_DIR}
  BOARD_UDS_PATH          default: ${BOARD_UDS_PATH}
  BOARD_SHM_PATH          default: ${BOARD_SHM_PATH}
  BOARD_BPU_CORE_PRIMARY  default: ${BOARD_BPU_CORE_PRIMARY}
  BOARD_PASSWORD          default: ${BOARD_PASSWORD}
  RUN_SECONDS             default: ${RUN_SECONDS}
  SERVER_PROCESS_NAME     default: ${SERVER_PROCESS_NAME}
  MANAGER_PROCESS_NAME    default: ${MANAGER_PROCESS_NAME}
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

if ! command -v "${SSH_BIN}" >/dev/null 2>&1; then
  echo "Error: ssh command not found: ${SSH_BIN}" >&2
  exit 127
fi

SSH_ARGS=("${SSH_BIN}")
if [[ -n "${BOARD_PASSWORD}" ]]; then
  if ! command -v sshpass >/dev/null 2>&1; then
    echo "Error: BOARD_PASSWORD is set but sshpass is not installed." >&2
    exit 127
  fi
  SSH_ARGS=(sshpass -p "${BOARD_PASSWORD}" "${SSH_BIN}" -o StrictHostKeyChecking=no)
fi

REMOTE_RUN_SCRIPT=$(cat <<EOF
set -euo pipefail
chmod +x '${SERVER_BINARY_REMOTE}' '${MANAGER_BINARY_REMOTE}'
export LD_LIBRARY_PATH='${BOARD_LIB_DIR}':\${LD_LIBRARY_PATH:-}
nohup '${SERVER_BINARY_REMOTE}' \
  --model '${BOARD_MODEL_PATH}' \
  --uds-path '${BOARD_UDS_PATH}' \
  --channel-mask '0x7' \
  --bpu-core-id '${BOARD_BPU_CORE_PRIMARY}' \
  --shm-path '${BOARD_SHM_PATH}' \
  >'${REMOTE_SERVER_LOG}' 2>&1 &
for _ in \$(seq 1 50); do
  if [[ -e '/dev/shm/$(basename "${BOARD_SHM_PATH}")' && \
        -S '${BOARD_UDS_PATH}' ]]; then
    break
  fi
  sleep 0.2
done
if [[ ! -e '/dev/shm/$(basename "${BOARD_SHM_PATH}")' ]]; then
  echo "shared memory not ready: ${BOARD_SHM_PATH}" >&2
  exit 1
fi
if [[ ! -S '${BOARD_UDS_PATH}' ]]; then
  echo "uds socket not ready: ${BOARD_UDS_PATH}" >&2
  exit 1
fi
nohup '${MANAGER_BINARY_REMOTE}' \
  --input-dir-0 '${BOARD_INPUT_DIR_0}' \
  --input-dir-1 '${BOARD_INPUT_DIR_1}' \
  --input-dir-2 '${BOARD_INPUT_DIR_2}' \
  --uds-path '${BOARD_UDS_PATH}' \
  --shm-path '${BOARD_SHM_PATH}' \
  >'${REMOTE_MANAGER_LOG}' 2>&1 &
EOF
)

echo "Starting mt_server and mt_manager on ${BOARD_HOST}"
"${SSH_ARGS[@]}" "${BOARD_HOST}" "${REMOTE_RUN_SCRIPT}"

echo "Processes are running. Sleeping ${RUN_SECONDS}s"
sleep "${RUN_SECONDS}"

REMOTE_STOP_SCRIPT=$(cat <<EOF
set -euo pipefail
pkill -x '${MANAGER_PROCESS_NAME}' 2>/dev/null || true
pkill -x '${SERVER_PROCESS_NAME}' 2>/dev/null || true
rm -f '${BOARD_UDS_PATH}'
rm -f '/dev/shm/$(basename "${BOARD_SHM_PATH}")'
EOF
)

echo "Stopping processes on ${BOARD_HOST}"
"${SSH_ARGS[@]}" "${BOARD_HOST}" "${REMOTE_STOP_SCRIPT}"

echo "Run complete."
echo "Logs are available on board under ${BOARD_LOG_DIR}."
