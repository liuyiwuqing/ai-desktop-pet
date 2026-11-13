#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
firmware_dir="${repo_root}/firmware"

python3 "${repo_root}/tools/ci/check_deps.py"
python3 "${repo_root}/tools/board_codegen.py" --check
python3 "${repo_root}/tools/ci/check_contracts.py"
"${repo_root}/tools/ci/test_host.sh"

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

# 协议测试使用同一锁文件解析出的真实 JSON 源码与 codec SDK 头文件。
bash "${repo_root}/tools/ci/test_ai_protocol_host.sh"
bash "${repo_root}/tools/ci/test_ai_wss_host.sh"
bash "${repo_root}/tools/ci/test_sr_frontend_host.sh"
