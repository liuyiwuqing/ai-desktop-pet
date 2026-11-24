#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sim_dir="${repo_root}/firmware/build-growth-sim"
components="${repo_root}/firmware/components"
mkdir -p "${sim_dir}"
cc -std=c17 -Wall -Wextra -Werror -pthread \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${components}/contracts/include" -I "${components}/core/include" \
    -I "${components}/storage/include" -I "${components}/svc_growth/include" \
    -I "${components}/svc_growth/private_include" \
    "${components}/svc_growth/src/growth_engine.c" \
    "${components}/svc_growth/src/companion_model.c" \
    "${components}/svc_growth/src/growth_store.c" \
    "${components}/svc_growth/src/growth_service.c" \
    "${components}/svc_growth/src/growth_environment.c" \
    "${repo_root}/tools/growth_simulator.c" -o "${sim_dir}/growth_simulator"
exec "${sim_dir}/growth_simulator" --state-dir "${sim_dir}/state" "$@"
