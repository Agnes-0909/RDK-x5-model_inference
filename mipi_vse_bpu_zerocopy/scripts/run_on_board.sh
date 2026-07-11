#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL="${MODEL:-/root/mipi_proj/model/model.bin}"
FRAMES="${FRAMES:-100}"
SCORE="${SCORE:-0.25}"
NMS="${NMS:-0.45}"
LOG_ROOT="${LOG_ROOT:-${SCRIPT_DIR}/logs}"
LOG_LEVEL="${LOG_LEVEL:-info}"
LOG_MAX_MB="${LOG_MAX_MB:-100}"
LOG_FILES="${LOG_FILES:-5}"
RUN_DIR="${LOG_ROOT}/run_$(date +%Y%m%d_%H%M%S)"

mkdir -p "${RUN_DIR}"
export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu:/usr/hobot/lib:${LD_LIBRARY_PATH:-}"

echo "Log directory: ${RUN_DIR}"
"${SCRIPT_DIR}/mipi_vse_bpu_zerocopy" \
  --model "${MODEL}" \
  --frames "${FRAMES}" \
  --score "${SCORE}" \
  --nms "${NMS}" \
  --log-dir "${RUN_DIR}" \
  --log-level "${LOG_LEVEL}" \
  --log-max-mb "${LOG_MAX_MB}" \
  --log-files "${LOG_FILES}" \
  "$@" 2>&1 | tee "${RUN_DIR}/console.log"
