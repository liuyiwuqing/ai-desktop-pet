#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sim_dir="${repo_root}/firmware/build-voice-sim"
components="${repo_root}/firmware/components"
mkdir -p "${sim_dir}"
cc -std=c17 -Wall -Wextra -Werror \
    -I "${components}/contracts/include" -I "${components}/pet_hal/include" \
    -I "${components}/svc_ai_agent/include" \
    "${components}/svc_ai_agent/src/ai_session.c" \
    "${components}/pet_hal/src/audio_pcm.c" \
    "${repo_root}/tools/voice_simulator.c" -lm -o "${sim_dir}/voice_simulator"
exec "${sim_dir}/voice_simulator" "$@"
