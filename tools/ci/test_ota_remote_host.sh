#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out="${root}/firmware/build-ota-remote-host"
json="${root}/firmware/managed_components/espressif__cjson/cJSON"
mkdir -p "${out}"
flags=(-std=c17 -Wall -Wextra -Werror -I "${root}/firmware/components/svc_ota/include"
    -I "${root}/firmware/components/contracts/include" -I "${root}/firmware/tests/host/support" -I "${json}")
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
fi
cc "${flags[@]}" -Wno-deprecated-declarations -c "${json}/cJSON.c" -o "${out}/cjson.o"
cc "${flags[@]}" "${root}/firmware/components/svc_ota/src/remote.c" \
    "${root}/firmware/tests/host/test_ota_remote.c" "${out}/cjson.o" -lm -o "${out}/test_ota_remote"
"${out}/test_ota_remote"
