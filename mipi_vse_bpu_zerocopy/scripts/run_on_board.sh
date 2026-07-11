#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODEL="${MODEL:-/root/mipi_proj/model/model.bin}"
FRAMES="${FRAMES:-100}"

export LD_LIBRARY_PATH="/usr/lib/aarch64-linux-gnu:/usr/hobot/lib:${LD_LIBRARY_PATH:-}"

exec "${SCRIPT_DIR}/mipi_vse_bpu_zerocopy" \
  --model "${MODEL}" \
  --frames "${FRAMES}" \
  "$@"
