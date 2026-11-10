#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
firmware_dir="${repo_root}/firmware"

if ! command -v idf.py >/dev/null 2>&1; then
    : "${IDF_PATH:?IDF_PATH must point to ESP-IDF 6.1}"
    if [[ -z "${IDF_PYTHON_ENV_PATH:-}" && -d "${HOME}/.espressif/tools/python/v6.1/venv" ]]; then
        export IDF_PYTHON_ENV_PATH="${HOME}/.espressif/tools/python/v6.1/venv"
    fi
    # shellcheck disable=SC1091
    source "${IDF_PATH}/export.sh"
fi

cd "${firmware_dir}"
if [[ ! -f sdkconfig ]]; then
    idf.py set-target esp32s3
fi

idf.py build
idf.py size
