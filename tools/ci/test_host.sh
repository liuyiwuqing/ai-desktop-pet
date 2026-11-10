#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${repo_root}/firmware/build-host"
test_bin="${out_dir}/test_event_contract"

mkdir -p "${out_dir}"
cc -std=c17 -Wall -Wextra -Werror \
    -I "${repo_root}/firmware/components/contracts/include" \
    "${repo_root}/firmware/tests/host/test_event_contract.c" \
    -o "${test_bin}"

"${test_bin}"
