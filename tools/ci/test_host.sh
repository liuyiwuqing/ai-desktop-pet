#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${repo_root}/firmware/build-host"
contracts_inc="${repo_root}/firmware/components/contracts/include"

mkdir -p "${out_dir}"

build_and_run() {
    local name="$1"
    shift
    cc -std=c17 -Wall -Wextra -Werror "$@" -o "${out_dir}/${name}"
    "${out_dir}/${name}"
}

build_and_run test_event_contract \
    -I "${contracts_inc}" \
    "${repo_root}/firmware/tests/host/test_event_contract.c"

build_and_run test_config_generated \
    -I "${contracts_inc}" \
    "${repo_root}/firmware/tests/host/test_config_generated.c"

for ext_dir in "${repo_root}"/extensions/*/; do
    [[ -f "${ext_dir}/extension.json" ]] || continue
    ext_id="$(basename "${ext_dir}")"
    build_and_run "test_${ext_id}_contract" \
        -I "${contracts_inc}" \
        -I "${ext_dir}/include" \
        "${ext_dir}/src/${ext_id}.c" \
        "${ext_dir}/test/host/test_${ext_id}_contract.c"
done
