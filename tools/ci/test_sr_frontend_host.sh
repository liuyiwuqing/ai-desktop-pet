#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sr_dir="${repo_root}/firmware/managed_components/espressif__esp-sr"
ai_dir="${repo_root}/firmware/components/svc_ai_agent"
out_dir="${repo_root}/firmware/build-host"
if [[ ! -f "${sr_dir}/include/esp32s3/esp_vadn_models.h" ]]; then
    echo 'Missing locked ESP-SR headers; run idf.py -C firmware reconfigure first.' >&2
    exit 1
fi
mkdir -p "${out_dir}"
flags=(-std=c17 -Wall -Wextra -Werror)
suffix=""
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="_sanitized"
fi
# 使用锁定版本的真实模型 API 头；Xtensa 推理库/分区/密码学是外部系统边界。
cc "${flags[@]}" -I "${repo_root}/firmware/tests/host/support/sr" \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${repo_root}/firmware/components/contracts/include" -I "${ai_dir}/include" \
    -I "${sr_dir}/include/esp32s3" -I "${sr_dir}/src/include" \
    "${ai_dir}/src/sr_esp.c" "${repo_root}/firmware/tests/host/test_sr_frontend.c" \
    -o "${out_dir}/test_sr_frontend${suffix}"
"${out_dir}/test_sr_frontend${suffix}"
cc "${flags[@]}" -D_POSIX_C_SOURCE=200112L \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${repo_root}/firmware/components/contracts/include" -I "${ai_dir}/include" \
    -I "${sr_dir}/include/esp32s3" \
    "${ai_dir}/src/dsp_esp.c" "${repo_root}/firmware/tests/host/test_dsp.c" \
    -o "${out_dir}/test_dsp${suffix}"
"${out_dir}/test_dsp${suffix}"
