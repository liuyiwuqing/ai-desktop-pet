#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${repo_root}/firmware/build-host"
ai="${repo_root}/firmware/components/svc_ai_agent"
support="${repo_root}/firmware/tests/host/support"
managed="${repo_root}/firmware/managed_components"
codec="${managed}/espressif__esp_audio_codec/include"
json="${managed}/espressif__cjson/cJSON"
ws="${managed}/espressif__esp_websocket_client/include"
: "${IDF_PATH:?IDF_PATH must point to ESP-IDF 6.1 (source export.sh)}"
http_parser="${IDF_PATH}/components/http_parser"
if [[ ! -f "${ws}/esp_websocket_client.h" || ! -f "${json}/cJSON.c" ]]; then
    echo 'Missing locked SDK dependencies; run idf.py -C firmware reconfigure first.' >&2
    exit 1
fi
mkdir -p "${out}"
flags=(-std=c17 -Wall -Wextra -Werror -pthread
    -I "${support}/xiaozhi_wss" -I "${support}/ai_agent" -I "${support}"
    -I "${ai}/include" -I "${repo_root}/firmware/components/pet_hal/include"
    -I "${repo_root}/firmware/components/svc_intent/include"
    -I "${repo_root}/firmware/components/svc_focus/include"
    -I "${repo_root}/firmware/components/svc_message_hub/include"
    -I "${repo_root}/firmware/main/app" -I "${repo_root}/firmware/components/storage/include"
    -I "${repo_root}/firmware/components/contracts/include"
    -I "${repo_root}/firmware/components/svc_net/include"
    -I "${ws}" -I "${json}" -I "${http_parser}" -I "${codec}" -I "${codec}/encoder"
    -I "${codec}/encoder/impl" -I "${codec}/decoder" -I "${codec}/decoder/impl")
suffix=""
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="_sanitized"
fi
if [[ "${PET_HOST_TSAN:-0}" == 1 ]]; then
    if [[ -n "${suffix}" ]]; then
        echo 'Run ASan/UBSan and ThreadSanitizer separately.' >&2
        exit 1
    fi
    flags+=(-g -O1 -fsanitize=thread)
    suffix="_tsan"
fi
cc "${flags[@]}" -Wno-deprecated-declarations -c "${json}/cJSON.c" \
    -o "${out}/cjson_wss${suffix}.o"
cc "${flags[@]}" -c "${http_parser}/http_parser.c" -o "${out}/http_parser_wss${suffix}.o"
cc "${flags[@]}" "${ai}/src/xiaozhi_wss.c" "${ai}/src/xiaozhi.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" "${ai}/src/opus_esp.c" "${ai}/src/ai_agent.c" "${ai}/src/ai_runtime.c" \
    "${ai}/src/ai_session.c" "${ai}/src/ai_context.c" "${ai}/src/ai_context_json.c" "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/components/svc_intent/src/intent_rpc.c" \
    "${repo_root}/firmware/main/app/ai_startup.c" "${repo_root}/firmware/main/app/app_tools.c" \
    "${repo_root}/firmware/tests/host/test_xiaozhi_wss.c" "${out}/cjson_wss${suffix}.o" \
    "${out}/http_parser_wss${suffix}.o" \
    -lm -o "${out}/test_xiaozhi_wss${suffix}"
for scenario in startup tools app-tools alloc-fail tx-queue-fail rx-queue-fail task-fail codec-fail handshake delayed-send playback first-audio-delay first-audio-timeout \
    queue-cancel barge-in destroy-fail graceful-close playback-stall decode-fail \
    socket-fail register-fail socket-start-fail send-fail handshake-timeout failure-race \
    network-gate rx-overflow malformed-offset assembly-timeout remote-close network-lost failure-backpressure \
    boot-missing boot-ca-error boot-empty-ca boot-start privacy; do
    "${out}/test_xiaozhi_wss${suffix}" "${scenario}"
done
