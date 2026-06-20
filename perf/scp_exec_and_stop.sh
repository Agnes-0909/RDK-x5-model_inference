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
MT_ROOT="${PROJECT_ROOT}/multi_thread_proj"

BOARD_HOST="${BOARD_HOST:-root@192.168.1.12}"
BOARD_BASE_DIR="${BOARD_BASE_DIR:-/root/multi_thread_proj}"
BOARD_BIN_DIR="${BOARD_BIN_DIR:-${BOARD_BASE_DIR}/bin}"
BOARD_LIB_DIR="${BOARD_LIB_DIR:-${BOARD_BASE_DIR}/lib}"
BOARD_MODEL_PATH="${BOARD_MODEL_PATH:-${BOARD_BASE_DIR}/model/model.bin}"
BOARD_INPUT_DIR_0="${BOARD_INPUT_DIR_0:-${BOARD_BASE_DIR}/input/channel0}"
BOARD_INPUT_DIR_1="${BOARD_INPUT_DIR_1:-${BOARD_BASE_DIR}/input/channel1}"
BOARD_INPUT_DIR_2="${BOARD_INPUT_DIR_2:-${BOARD_BASE_DIR}/input/channel2}"
BOARD_LOG_DIR="${BOARD_LOG_DIR:-${BOARD_BASE_DIR}/log}"
BOARD_UDS_PATH="${BOARD_UDS_PATH:-/tmp/mt_infer.sock}"
BOARD_SHM_PATH="${BOARD_SHM_PATH:-/mt_infer_shm}"
BOARD_BPU_CORE_PRIMARY="${BOARD_BPU_CORE_PRIMARY:-0}"
BOARD_PASSWORD="${BOARD_PASSWORD:-root}"
RUN_SECONDS="${RUN_SECONDS:-60}"
SCP_BIN="${SCP_BIN:-scp}"
SSH_BIN="${SSH_BIN:-ssh}"
SERVER_BINARY_LOCAL="${SERVER_BINARY_LOCAL:-${MT_ROOT}/build_aarch64/mt_server}"
MANAGER_BINARY_LOCAL="${MANAGER_BINARY_LOCAL:-${MT_ROOT}/build_aarch64/mt_manager}"
SERVER_BINARY_REMOTE="${BOARD_BIN_DIR}/mt_server"
MANAGER_BINARY_REMOTE="${BOARD_BIN_DIR}/mt_manager"
SERVER_PROCESS_NAME="${SERVER_PROCESS_NAME:-mt_server}"
MANAGER_PROCESS_NAME="${MANAGER_PROCESS_NAME:-mt_manager}"
REMOTE_SERVER_LOG="${BOARD_LOG_DIR}/mt_server.log"
REMOTE_MANAGER_LOG="${BOARD_LOG_DIR}/mt_manager.log"

print_usage() {
  cat <<EOF
Usage: $(basename "$0")

Deploy mt_server and mt_manager to the target board, start them, let them run
for RUN_SECONDS, then stop the processes.

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
  BOARD_SHM_PATH          default: ${BOARD_SHM_PATH}
  BOARD_BPU_CORE_PRIMARY  default: ${BOARD_BPU_CORE_PRIMARY}
  BOARD_PASSWORD          default: empty (when set, use sshpass)
  SERVER_PROCESS_NAME     default: ${SERVER_PROCESS_NAME}
  MANAGER_PROCESS_NAME    default: ${MANAGER_PROCESS_NAME}
  RUN_SECONDS             default: ${RUN_SECONDS}
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

echo "Done. Logs are kept on board under ${BOARD_LOG_DIR}."
echo "Fetch them with: bash ${PROJECT_ROOT}/perf/scp_log_from_board.sh"
