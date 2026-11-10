#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${repo_root}/firmware/build-host"
contracts_inc="${repo_root}/firmware/components/contracts/include"
hal_inc="${repo_root}/firmware/components/pet_hal/include"
climate_priv_inc="${repo_root}/firmware/components/svc_climate/private_include"
presentation_inc="${repo_root}/firmware/components/presentation/include"
message_priv_inc="${repo_root}/firmware/components/svc_message_hub/private_include"

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

build_and_run test_climate_decode \
    -I "${contracts_inc}" \
    -I "${hal_inc}" \
    "${repo_root}/firmware/components/pet_hal/src/climate_decode.c" \
    "${repo_root}/firmware/tests/host/test_climate_decode.c"

build_and_run test_climate_pipeline \
    -I "${contracts_inc}" \
    -I "${climate_priv_inc}" \
    "${repo_root}/firmware/components/svc_climate/src/climate_pipeline.c" \
    "${repo_root}/firmware/tests/host/test_climate_pipeline.c"

build_and_run test_climate_alert \
    -I "${contracts_inc}" \
    -I "${climate_priv_inc}" \
    "${repo_root}/firmware/components/svc_climate/src/climate_alert.c" \
    "${repo_root}/firmware/tests/host/test_climate_alert.c"

build_and_run test_climate_state \
    -I "${contracts_inc}" \
    -I "${climate_priv_inc}" \
    "${repo_root}/firmware/components/svc_climate/src/climate_state.c" \
    "${repo_root}/firmware/tests/host/test_climate_state.c"

build_and_run test_climate_card \
    -I "${contracts_inc}" \
    -I "${presentation_inc}" \
    "${repo_root}/firmware/components/presentation/src/climate_card.c" \
    "${repo_root}/firmware/tests/host/test_climate_card.c"

build_and_run test_message_time \
    -I "${contracts_inc}" \
    -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_time.c" \
    "${repo_root}/firmware/tests/host/test_message_time.c"

build_and_run test_message_validate \
    -I "${contracts_inc}" \
    -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_validate.c" \
    "${repo_root}/firmware/tests/host/test_message_validate.c"

build_and_run test_message_queue \
    -I "${contracts_inc}" \
    -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_validate.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_queue.c" \
    "${repo_root}/firmware/tests/host/test_message_queue.c"

for ext_dir in "${repo_root}"/extensions/*/; do
    [[ -f "${ext_dir}/extension.json" ]] || continue
    ext_id="$(basename "${ext_dir}")"
    build_and_run "test_${ext_id}_contract" \
        -I "${contracts_inc}" \
        -I "${ext_dir}/include" \
        "${ext_dir}/src/${ext_id}.c" \
        "${ext_dir}/test/host/test_${ext_id}_contract.c"
done
