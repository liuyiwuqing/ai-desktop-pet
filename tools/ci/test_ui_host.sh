#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# LVGL 由固件的锁文件下载；主机渲染使用同一份源和二进制字体。
ui_build="${repo_root}/firmware/build-ui-preview"
# Keep the array nonempty: macOS Bash 3.2 rejects an empty array with nounset.
cmake_options=(-S "${repo_root}/firmware/tests/host")
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    ui_build+="-sanitize"
    sanitizer_flags='-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all'
    cmake_options+=("-DCMAKE_C_FLAGS=${sanitizer_flags}" "-DCMAKE_CXX_FLAGS=${sanitizer_flags}"
        '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined')
fi
cmake "${cmake_options[@]}" -B "$ui_build"
cmake --build "$ui_build" -j 6
ctest --test-dir "$ui_build" --output-on-failure
