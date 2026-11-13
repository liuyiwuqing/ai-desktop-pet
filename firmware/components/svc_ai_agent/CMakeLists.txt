set(ai_sources "")
if(CONFIG_PET_MODULE_AI_ENABLED)
    list(APPEND ai_sources "src/ai_session.c" "src/ai_context.c" "src/ai_context_json.c")
    list(APPEND ai_sources "src/ai_runtime.c")
    if(CONFIG_PET_MODULE_AUDIO_ENABLED)
        list(APPEND ai_sources "src/ai_agent.c")
        if(CONFIG_PET_AI_DSP_ENABLED)
            list(APPEND ai_sources "src/dsp_esp.c")
        endif()
        if(CONFIG_PET_AI_ESP_SR_ENABLED)
            list(APPEND ai_sources "src/sr_esp.c")
        endif()
        if(CONFIG_PET_AI_XIAOZHI_ENABLED)
            list(APPEND ai_sources "src/xiaozhi.c" "src/opus_esp.c")
            if(CONFIG_PET_AI_XIAOZHI_WSS_ENABLED)
                list(APPEND ai_sources "src/xiaozhi_wss.c")
            endif()
        endif()
    endif()
endif()

idf_component_register(
    SRCS ${ai_sources}
    INCLUDE_DIRS "include"
    REQUIRES "contracts" "pet_hal"
    # IDF 在 Kconfig 展开前收集依赖；依赖固定，源文件单独按功能门控。
    PRIV_REQUIRES "esp_timer" "heap" "freertos" "cjson" "esp_audio_codec"
        "svc_net" "esp_netif" "esp_websocket_client" "mbedtls" "http_parser"
        "esp-sr" "esp_partition"
)

if(CONFIG_PET_AI_ESP_SR_ENABLED)
    if(NOT CONFIG_MODEL_IN_FLASH OR NOT CONFIG_SR_WN_WN9_XIAOAITONGXUE OR
       NOT CONFIG_SR_VADN_VADNET1_MEDIUM)
        message(FATAL_ERROR "ESP-SR requires the fixed model pair; use sdkconfig.ai-sr.defaults")
    endif()
    idf_build_get_property(sr_build_dir BUILD_DIR)
    set(sr_image "${sr_build_dir}/srmodels/srmodels.bin")
    set(sr_header "${sr_build_dir}/pet_sr/pet_sr_model_info.h")
    set(sr_checker "${CMAKE_CURRENT_LIST_DIR}/../../../tools/check_sr_models.py")
    set(sr_partitions "${CMAKE_CURRENT_LIST_DIR}/../../partitions/16m.csv")
    add_custom_command(OUTPUT "${sr_header}"
        COMMAND ${PYTHON} "${sr_checker}" --image "${sr_image}"
            --partition-table "${sr_partitions}" --header "${sr_header}"
        DEPENDS "${sr_image}" "${sr_checker}" "${sr_partitions}"
        VERBATIM)
    add_custom_target(pet_sr_model_check DEPENDS "${sr_header}")
    add_dependencies(${COMPONENT_LIB} pet_sr_model_check)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${sr_build_dir}/pet_sr")
endif()
