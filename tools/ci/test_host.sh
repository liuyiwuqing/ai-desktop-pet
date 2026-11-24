#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
out_dir="${repo_root}/firmware/build-host"
contracts_inc="${repo_root}/firmware/components/contracts/include"
hal_inc="${repo_root}/firmware/components/pet_hal/include"
climate_priv_inc="${repo_root}/firmware/components/svc_climate/private_include"
presentation_inc="${repo_root}/firmware/components/presentation/include"
message_priv_inc="${repo_root}/firmware/components/svc_message_hub/private_include"
net_priv_inc="${repo_root}/firmware/components/svc_net/private_include"
growth_priv_inc="${repo_root}/firmware/components/svc_growth/private_include"

mkdir -p "${out_dir}"

host_flags=(-std=c17 -Wall -Wextra -Werror)
if [[ "${PET_HOST_SANITIZE:-0}" == 1 ]]; then
    host_flags+=(-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer)
fi

build_and_run() {
    local name="$1"
    shift
    cc "${host_flags[@]}" "$@" -o "${out_dir}/${name}"
    "${out_dir}/${name}"
}

python3 "${repo_root}/tools/ci/test_board_codegen.py"
python3 "${repo_root}/tools/ci/test_voice_simulator.py"
python3 "${repo_root}/tools/ci/test_sr_models.py"
python3 "${repo_root}/tools/ci/test_control_gateway.py"

build_and_run test_pet_game -I "${presentation_inc}" \
    "${repo_root}/firmware/components/presentation/src/pet_game.c" \
    "${repo_root}/firmware/tests/host/test_pet_game.c"

build_and_run test_event_bus \
    -I "${repo_root}/firmware/tests/host/support/event_bus" \
    -I "${repo_root}/firmware/tests/host/support/ai_agent" \
    -I "${repo_root}/firmware/tests/host/support" -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/core/include" \
    "${repo_root}/firmware/components/core/src/event_bus.c" \
    "${repo_root}/firmware/tests/host/test_event_bus.c"

cc "${host_flags[@]}" -Wno-deprecated-declarations \
    -I "${repo_root}/firmware/managed_components/espressif__cjson/cJSON" \
    -c "${repo_root}/firmware/managed_components/espressif__cjson/cJSON/cJSON.c" \
    -o "${out_dir}/setup_cjson.o"

build_and_run test_setup_policy \
    -I "${repo_root}/firmware/tests/host/support" -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/svc_setup/include" \
    -I "${repo_root}/firmware/components/svc_setup/private_include" \
    -I "${repo_root}/firmware/managed_components/espressif__cjson/cJSON" \
    "${repo_root}/firmware/components/svc_setup/src/setup_policy.c" \
    "${out_dir}/setup_cjson.o" \
    "${repo_root}/firmware/tests/host/test_setup_policy.c" -lm

for fallback in '' compiled-network; do
    build_and_run test_setup_boot \
        -D"CONFIG_PET_NET_WIFI_SSID=\"${fallback}\"" \
        -I "${repo_root}/firmware/tests/host/support/setup_boot" \
        -I "${repo_root}/firmware/tests/host/support/ai_agent" \
        -I "${repo_root}/firmware/tests/host/support" -I "${contracts_inc}" \
        -I "${repo_root}/firmware/components/svc_config/include" \
        -I "${repo_root}/firmware/components/storage/include" \
        -I "${repo_root}/firmware/components/svc_setup/include" \
        "${repo_root}/firmware/components/svc_setup/src/setup_boot.c" \
        "${repo_root}/firmware/tests/host/test_setup_boot.c"
done

build_and_run test_setup_activation \
    -I "${repo_root}/firmware/tests/host/support" -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/svc_setup/private_include" \
    -I "${repo_root}/firmware/managed_components/espressif__cjson/cJSON" \
    "${repo_root}/firmware/components/svc_setup/src/setup_activation_policy.c" \
    "${out_dir}/setup_cjson.o" \
    "${repo_root}/firmware/tests/host/test_setup_activation.c" -lm

build_and_run test_setup_portal \
    -I "${repo_root}/firmware/tests/host/support" -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/svc_setup/include" \
    -I "${repo_root}/firmware/components/svc_setup/private_include" \
    -I "${repo_root}/firmware/managed_components/espressif__cjson/cJSON" \
    "${repo_root}/firmware/components/svc_setup/src/setup_policy.c" \
    "${out_dir}/setup_cjson.o" \
    "${repo_root}/firmware/tests/host/test_setup_portal.c" -lm

bash "${repo_root}/tools/ci/test_config_host.sh"
bash "${repo_root}/tools/ci/test_ota_host.sh"
bash "${repo_root}/tools/ci/test_ota_remote_host.sh"

build_and_run test_ai_session \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_ai_agent/include" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_session.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/tests/host/test_ai_session.c" -lm

build_and_run test_ai_runtime \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_ai_agent/include" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_session.c" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_runtime.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/tests/host/test_ai_runtime.c" -lm

build_and_run test_ai_agent \
    -pthread -I "${repo_root}/firmware/tests/host/support/ai_agent" \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_ai_agent/include" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_agent.c" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_context.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_session.c" \
    "${repo_root}/firmware/components/svc_ai_agent/src/ai_runtime.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/tests/host/test_ai_agent.c" -lm
for scenario in notification notification-only notification-critical prebuffer jitter-buffer wake alloc-fail control-queue-fail playback-queue-fail task-fail init-fail \
    capture-fail read-fail read-overrun deinit-fail read-stall partial-read burst-read transport-fail \
    stop-fail write-fail partial-write write-stall drain-fail queue-full cancel-fail \
    empty-playback playback-stall barge-in priority-mute priority-cancel privacy privacy-read \
    privacy-wake privacy-playback privacy-stop-fail privacy-concurrent privacy-patches \
    frontend-open-fail frontend-reset-fail frontend-lifecycle frontend-policy-wake frontend-cancel-wake \
    frontend-wake-fail frontend-vad-fail frontend-vad-invalid; do
    "${out_dir}/test_ai_agent" "${scenario}"
done

build_and_run test_audio_pcm \
    -I "${contracts_inc}" -I "${hal_inc}" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/tests/host/test_audio_pcm.c" -lm

build_and_run test_audio_buffer \
    -I "${contracts_inc}" -I "${hal_inc}" \
    "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" \
    "${repo_root}/firmware/tests/host/test_audio_buffer.c"

build_and_run test_audio_loopback \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_audio/include" \
    "${repo_root}/firmware/components/svc_audio/src/audio_loopback.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" \
    "${repo_root}/firmware/tests/host/test_audio_loopback.c" -lm

build_and_run test_audio_fake \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_audio/include" \
    "${repo_root}/firmware/components/svc_audio/src/audio_loopback.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" \
    "${repo_root}/firmware/components/pet_hal/src/fake_audio.c" \
    "${repo_root}/firmware/tests/host/test_audio_fake.c" -lm

build_and_run test_audio_duplex \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/svc_audio/include" \
    "${repo_root}/firmware/components/svc_audio/src/audio_duplex.c" \
    "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
    "${repo_root}/firmware/tests/host/test_audio_duplex.c" -lm

for mode in 0 1; do
    build_and_run "test_audio_diagnostic_${mode}" \
        -DCONFIG_PET_AUDIO_DIAGNOSTIC_DUPLEX="${mode}" \
        -I "${repo_root}/firmware/tests/host/support/audio_diagnostic" \
        -I "${repo_root}/firmware/tests/host/support/ai_agent" \
        -I "${repo_root}/firmware/tests/host/support" \
        -I "${contracts_inc}" -I "${hal_inc}" \
        -I "${repo_root}/firmware/components/svc_audio/include" \
        "${repo_root}/firmware/components/svc_audio/src/audio_diagnostic.c" \
        "${repo_root}/firmware/components/svc_audio/src/audio_loopback.c" \
        "${repo_root}/firmware/components/svc_audio/src/audio_duplex.c" \
        "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
        "${repo_root}/firmware/components/pet_hal/src/audio_buffer.c" \
        "${repo_root}/firmware/tests/host/test_audio_diagnostic.c" -lm
    for scenario in before init mid allocation task; do
        "${out_dir}/test_audio_diagnostic_${mode}" "${scenario}"
    done
done

for variant in default pin-conflict; do
    audio_flags=()
    if [[ "${variant}" == pin-conflict ]]; then
        audio_flags=(-DCONFIG_PET_CLIMATE_DHT11_GPIO=17)
    fi
    build_and_run "test_bsp_audio_${variant}" \
        ${audio_flags[@]+"${audio_flags[@]}"} \
        -I "${repo_root}/firmware/tests/host/support/audio_bsp" \
        -I "${repo_root}/firmware/tests/host/support" \
        -I "${contracts_inc}" -I "${hal_inc}" \
        -I "${repo_root}/firmware/components/bsp_dodo_s3_lcd/private_include" \
        "${repo_root}/firmware/components/bsp_dodo_s3_lcd/src/bsp_audio.c" \
        "${repo_root}/firmware/components/pet_hal/src/audio_pcm.c" \
        "${repo_root}/firmware/tests/host/test_bsp_audio.c" -lm
done

build_and_run test_event_contract \
    -I "${contracts_inc}" \
    "${repo_root}/firmware/tests/host/test_event_contract.c"

build_and_run test_config_generated \
    -I "${contracts_inc}" \
    "${repo_root}/firmware/tests/host/test_config_generated.c"

build_and_run test_growth \
    -I "${contracts_inc}" -I "${growth_priv_inc}" \
    "${repo_root}/firmware/components/svc_growth/src/growth_engine.c" \
    "${repo_root}/firmware/tests/host/test_growth.c"

build_and_run test_growth_sync \
    -I "${contracts_inc}" -I "${growth_priv_inc}" \
    -I "${repo_root}/firmware/components/svc_growth/include" \
    "${repo_root}/firmware/components/svc_growth/src/growth_engine.c" \
    "${repo_root}/firmware/components/svc_growth/src/growth_sync.c" \
    "${repo_root}/firmware/tests/host/test_growth_sync.c"

build_and_run test_growth_service \
    -pthread -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${growth_priv_inc}" \
    -I "${repo_root}/firmware/components/core/include" \
    -I "${repo_root}/firmware/components/storage/include" \
    -I "${repo_root}/firmware/components/svc_growth/include" \
    "${repo_root}/firmware/components/svc_growth/src/growth_engine.c" \
    "${repo_root}/firmware/components/svc_growth/src/growth_store.c" \
    "${repo_root}/firmware/components/svc_growth/src/growth_service.c" \
    "${repo_root}/firmware/components/svc_growth/src/growth_environment.c" \
    "${repo_root}/firmware/tests/host/test_growth_service.c"

build_and_run test_growth_runtime \
    -I "${repo_root}/firmware/tests/host/support/growth_runtime" \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${repo_root}/firmware/components/core/include" \
    -I "${repo_root}/firmware/components/svc_growth/include" \
    "${repo_root}/firmware/components/svc_growth/src/growth_runtime.c" \
    "${repo_root}/firmware/tests/host/test_growth_runtime.c"
for scenario in init-fail queue-fail task-fail subscribe-fail; do
    "${out_dir}/test_growth_runtime" "${scenario}"
done

build_and_run test_growth_runtime_no_climate \
    -DCONFIG_PET_MODULE_CLIMATE_ENABLED=0 \
    -I "${repo_root}/firmware/tests/host/support/growth_runtime" \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${repo_root}/firmware/components/core/include" \
    -I "${repo_root}/firmware/components/svc_growth/include" \
    "${repo_root}/firmware/components/svc_growth/src/growth_runtime.c" \
    "${repo_root}/firmware/tests/host/test_growth_runtime.c"
"${out_dir}/test_growth_runtime_no_climate" init-fail
"${out_dir}/test_growth_runtime_no_climate" task-fail

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

build_and_run test_message_codec \
    -I "${contracts_inc}" \
    -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_time.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_codec.c" \
    "${repo_root}/firmware/tests/host/test_message_codec.c"

build_and_run test_nexus_codec \
    -I "${contracts_inc}" -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_time.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_codec.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_validate.c" \
    "${repo_root}/firmware/tests/host/test_nexus_codec.c"

build_and_run test_message_store \
    -D_POSIX_C_SOURCE=200112L \
    -DTEST_FIXTURE_DIR="\"${repo_root}/firmware/tests/host/fixtures\"" \
    -I "${contracts_inc}" \
    -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_store_codec.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_validate.c" \
    "${repo_root}/firmware/tests/host/test_message_store.c"

build_and_run test_message_presenter \
    -I "${contracts_inc}" -I "${presentation_inc}" -I "${message_priv_inc}" \
    "${repo_root}/firmware/components/presentation/src/message_presenter.c" \
    "${repo_root}/firmware/components/svc_message_hub/src/message_validate.c" \
    "${repo_root}/firmware/tests/host/test_message_presenter.c"

build_and_run test_message_service \
    -D_POSIX_C_SOURCE=200112L \
    -DTEST_FIXTURE_DIR="\"${repo_root}/firmware/tests/host/fixtures\"" \
    -I "${repo_root}/firmware/tests/host/support" \
    -I "${contracts_inc}" -I "${message_priv_inc}" \
    -I "${net_priv_inc}" \
    -I "${repo_root}/firmware/components/core/include" \
    -I "${repo_root}/firmware/components/storage/include" \
    -I "${repo_root}/firmware/components/svc_message_hub/include" \
    "${repo_root}"/firmware/components/svc_message_hub/src/*.c \
    "${repo_root}/firmware/components/storage/src/storage.c" \
    "${repo_root}/firmware/components/svc_net/src/net_policy.c" \
    "${repo_root}/firmware/tests/host/test_message_service.c"
"${out_dir}/test_message_service" storage-failed
"${out_dir}/test_message_service" capacity
"${out_dir}/test_message_service" limit
"${out_dir}/test_message_service" inbox-capacity
"${out_dir}/test_message_service" concurrent-upgrade
"${out_dir}/test_message_service" snapshot-space
"${out_dir}/test_message_service" legacy-migration
"${out_dir}/test_message_service" identity
"${out_dir}/test_message_service" save-read "${out_dir}/power-cut-nvs.bin"
"${out_dir}/test_message_service" restore-read "${out_dir}/power-cut-nvs.bin"
"${out_dir}/test_message_service" restore-other-device "${out_dir}/power-cut-nvs.bin"
"${out_dir}/test_message_service" save-confirmed "${out_dir}/power-cut-nvs.bin"
"${out_dir}/test_message_service" restore-confirmed "${out_dir}/power-cut-nvs.bin"
rm "${out_dir}/power-cut-nvs.bin"

build_and_run test_net_policy \
    -I "${contracts_inc}" \
    -I "${net_priv_inc}" \
    "${repo_root}/firmware/components/svc_net/src/net_policy.c" \
    "${repo_root}/firmware/tests/host/test_net_policy.c"

for ext_dir in "${repo_root}"/extensions/*/; do
    [[ -f "${ext_dir}/extension.json" ]] || continue
    ext_id="$(basename "${ext_dir}")"
    build_and_run "test_${ext_id}_contract" \
        -I "${contracts_inc}" \
        -I "${ext_dir}/include" \
        "${ext_dir}/src/${ext_id}.c" \
        "${ext_dir}/test/host/test_${ext_id}_contract.c"
done

build_and_run test_navigation \
    -I "${hal_inc}" \
    "${repo_root}/firmware/components/pet_hal/src/navigation.c" \
    "${repo_root}/firmware/tests/host/test_navigation.c"

build_and_run test_key_input \
    -I "${hal_inc}" \
    -I "${repo_root}/firmware/components/ui_lvgl/src" \
    "${repo_root}/firmware/components/pet_hal/src/keys.c" \
    "${repo_root}/firmware/components/ui_lvgl/src/key_actions.c" \
    "${repo_root}/firmware/tests/host/test_key_input.c"

build_and_run test_ambient_light \
    -I "${hal_inc}" \
    "${repo_root}/firmware/components/pet_hal/src/ambient_light.c" \
    "${repo_root}/firmware/tests/host/test_ambient_light.c"

for variant in both joystick light neither inverted; do
    input_flags=()
    case "${variant}" in
        joystick) input_flags=(-DCONFIG_PET_EXT_LIGHT_SENSOR=0) ;;
        light) input_flags=(-DCONFIG_PET_EXT_JOYSTICK=0) ;;
        neither) input_flags=(-DCONFIG_PET_EXT_JOYSTICK=0 -DCONFIG_PET_EXT_LIGHT_SENSOR=0) ;;
        inverted) input_flags=(-DCONFIG_PET_JOYSTICK_INVERT_X=1 -DCONFIG_PET_JOYSTICK_INVERT_Y=1 -DCONFIG_PET_LIGHT_SENSOR_INVERT=1) ;;
    esac
build_and_run "test_bsp_input_${variant}" \
    ${input_flags[@]+"${input_flags[@]}"} \
    -I "${repo_root}/firmware/tests/host/support/input_bsp" \
    -I "${repo_root}/firmware/tests/host/support" -I "${hal_inc}" -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/bsp_dodo_s3_lcd/private_include" \
    "${repo_root}/firmware/components/bsp_dodo_s3_lcd/src/bsp_input.c" \
    "${repo_root}/firmware/components/bsp_dodo_s3_lcd/src/bsp_analog.c" \
    "${repo_root}/firmware/components/bsp_dodo_s3_lcd/src/bsp_light.c" \
    "${repo_root}/firmware/tests/host/test_bsp_input.c"
done

build_and_run test_menu_def \
    -I "${hal_inc}" \
    -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/ui_lvgl/src" \
    "${repo_root}/firmware/components/ui_lvgl/src/menu_def.c" \
    "${repo_root}/firmware/tests/host/test_menu_def.c"

build_and_run test_growth_input \
    -I "${hal_inc}" \
    -I "${contracts_inc}" \
    -I "${repo_root}/firmware/components/ui_lvgl/src" \
    "${repo_root}/firmware/components/ui_lvgl/src/menu_def.c" \
    "${repo_root}/firmware/components/ui_lvgl/src/growth_input.c" \
    "${repo_root}/firmware/tests/host/test_growth_input.c"

build_and_run test_pet_motion \
    -I "${contracts_inc}" -I "${presentation_inc}" \
    "${repo_root}/firmware/components/presentation/src/pet_motion.c" \
    "${repo_root}/firmware/tests/host/test_pet_motion.c"

build_and_run test_focus -I "${repo_root}/firmware/components/svc_focus/include" \
    "${repo_root}/firmware/components/svc_focus/src/focus_engine.c" \
    "${repo_root}/firmware/tests/host/test_focus.c"

build_and_run test_command -I "${contracts_inc}" -I "${repo_root}/extensions/device_control/include" \
    "${repo_root}/extensions/device_control/src/command.c" \
    "${repo_root}/extensions/device_control/test/host/test_command.c"
build_and_run test_actuator -I "${repo_root}/extensions/servo/include" \
    "${repo_root}/extensions/servo/src/servo_engine.c" \
    "${repo_root}/extensions/servo/test/host/test_actuator.c"
build_and_run test_control_confirmation -I "${repo_root}/firmware/components/ui_lvgl/src" \
    "${repo_root}/firmware/components/ui_lvgl/src/control_confirmation.c" \
    "${repo_root}/firmware/tests/host/test_control_confirmation.c"

# The same boot-loop policy used by the target; RTC/reset inputs are the boundary.
build_and_run test_recovery -I "${repo_root}/firmware/main/app" \
    "${repo_root}/firmware/main/app/recovery_engine.c" \
    "${repo_root}/firmware/tests/host/test_recovery.c"
build_and_run test_recovery_presenter -I "${presentation_inc}" \
    "${repo_root}/firmware/components/presentation/src/recovery_presenter.c" \
    "${repo_root}/firmware/tests/host/test_recovery_presenter.c"
python3 "${repo_root}/tools/ci/test_build_matrix.py"
python3 "${repo_root}/tools/ci/test_crash_dump.py"
