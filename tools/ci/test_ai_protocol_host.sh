#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${repo_root}/firmware/build-host"
ai_dir="${repo_root}/firmware/components/svc_ai_agent"
cjson_dir="${repo_root}/firmware/managed_components/espressif__cjson/cJSON"
codec_inc="${repo_root}/firmware/managed_components/espressif__esp_audio_codec/include"
if [[ ! -f "${cjson_dir}/cJSON.c" || ! -f "${codec_inc}/encoder/impl/esp_opus_enc.h" ]]; then
    echo 'Missing locked SDK dependencies; run idf.py -C firmware reconfigure first.' >&2
    exit 1
fi
mkdir -p "${out_dir}"
common=(-std=c17 -Wall -Wextra -Werror
    -I "${repo_root}/firmware/components/contracts/include" -I "${ai_dir}/include")
suffix=""
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    common+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="_sanitized"
fi
# Xcode 的优化构建将上游 sprintf 标记为 deprecated；只对第三方源屏蔽此告警。
cc "${common[@]}" -Wno-deprecated-declarations -I "${cjson_dir}" \
    -c "${cjson_dir}/cJSON.c" -o "${out_dir}/cjson_protocol${suffix}.o"
cc "${common[@]}" -I "${cjson_dir}" \
    "${ai_dir}/src/xiaozhi.c" "${out_dir}/cjson_protocol${suffix}.o" \
    "${repo_root}/firmware/tests/host/test_xiaozhi.c" -lm -o "${out_dir}/test_xiaozhi${suffix}"
"${out_dir}/test_xiaozhi${suffix}"

cc "${common[@]}" -I "${repo_root}/firmware/tests/host/support" \
    -I "${codec_inc}" -I "${codec_inc}/encoder" -I "${codec_inc}/encoder/impl" \
    -I "${codec_inc}/decoder" -I "${codec_inc}/decoder/impl" \
    "${ai_dir}/src/opus_esp.c" "${repo_root}/firmware/tests/host/test_opus_port.c" \
    -o "${out_dir}/test_opus_port${suffix}"
"${out_dir}/test_opus_port${suffix}"

cc "${common[@]}" -I "${cjson_dir}" \
    "${ai_dir}/src/ai_context.c" "${ai_dir}/src/ai_context_json.c" "${out_dir}/cjson_protocol${suffix}.o" \
    "${repo_root}/firmware/tests/host/test_ai_context.c" -lm -o "${out_dir}/test_ai_context${suffix}"
"${out_dir}/test_ai_context${suffix}"

cc "${common[@]}" -I "${cjson_dir}" -I "${repo_root}/firmware/components/svc_intent/include" \
    "${repo_root}/firmware/components/svc_intent/src/intent_rpc.c" "${out_dir}/cjson_protocol${suffix}.o" \
    "${repo_root}/firmware/tests/host/test_intent.c" -lm -o "${out_dir}/test_intent${suffix}"
"${out_dir}/test_intent${suffix}"
