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

if [[ ! -x "${BUILD_SCRIPT_WRAPPER}" && ! -f "${BUILD_SCRIPT_WRAPPER}" ]]; then
  echo "Error: build wrapper not found: ${BUILD_SCRIPT_WRAPPER}" >&2
  exit 1
fi

exec bash "${BUILD_SCRIPT_WRAPPER}" "$@"
