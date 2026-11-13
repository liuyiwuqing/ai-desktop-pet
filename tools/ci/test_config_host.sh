#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${repo_root}/firmware/build-host"
component="${repo_root}/firmware/components"
json="${repo_root}/firmware/managed_components/espressif__cjson/cJSON"
mkdir -p "${out}"
flags=(-std=c17 -Wall -Wextra -Werror -pthread
    -I "${repo_root}/firmware/tests/host/support/ai_agent"
    -I "${repo_root}/firmware/tests/host/support" -I "${component}/contracts/include"
    -I "${component}/storage/include" -I "${component}/svc_config/include" -I "${json}")
suffix=""
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
    suffix="_sanitized"
fi
cc "${flags[@]}" -Wno-deprecated-declarations -c "${json}/cJSON.c" -o "${out}/cjson_config${suffix}.o"
cc "${flags[@]}" "${component}/svc_config/src/config_service.c" \
    "${component}/svc_config/src/config_codec.c" "${component}/svc_config/src/provision.c" \
    "${component}/svc_config/src/config_remote.c" \
    "${component}/svc_config/src/config_worker.c" \
    "${repo_root}/firmware/tests/host/test_config_service.c" "${out}/cjson_config${suffix}.o" \
    -lm -o "${out}/test_config_service${suffix}"
"${out}/test_config_service${suffix}"
