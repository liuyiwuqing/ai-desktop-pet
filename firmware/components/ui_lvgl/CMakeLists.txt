set(ui_sources "src/ui_service.c")
if(CONFIG_PET_MODULE_UI_ENABLED)
    list(APPEND ui_sources "src/message_view.c" "src/ui_owner.c")
    if(CONFIG_PET_INPUT_TRIPLE)
        list(APPEND ui_sources "src/key_actions.c")
    endif()
    if(CONFIG_PET_SETUP_PORTAL_ENABLED)
        list(APPEND ui_sources "src/setup_ui.c" "src/setup_view.c")
    endif()
    if(CONFIG_PET_RECOVERY_ENABLED)
        list(APPEND ui_sources "src/recovery_view.c" "src/recovery_ui.c")
    endif()
    if(CONFIG_PET_EXT_DEVICE_CONTROL)
        list(APPEND ui_sources "src/control_confirmation.c" "src/control_view.c")
    endif()
    if(CONFIG_PET_MODULE_CLIMATE_ENABLED)
        list(APPEND ui_sources "src/climate_view.c")
    endif()
    if(CONFIG_PET_MODULE_GROWTH_ENABLED OR
       (CONFIG_PET_MODULE_AI_ENABLED AND CONFIG_PET_MODULE_AUDIO_ENABLED AND NOT CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT))
        list(APPEND ui_sources "src/growth_input.c" "src/pet_avatar.c")
    endif()
    if(CONFIG_PET_MODULE_AI_ENABLED AND CONFIG_PET_MODULE_AUDIO_ENABLED AND NOT CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT)
        list(APPEND ui_sources "src/ai_view.c")
    endif()
    if(CONFIG_PET_MODULE_GROWTH_ENABLED)
        list(APPEND ui_sources "src/growth_view.c")
    endif()
endif()
idf_component_register(
    SRCS ${ui_sources}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES "contracts" "pet_hal"
    PRIV_REQUIRES "presentation" "svc_growth" "svc_ai_agent" "svc_config" "svc_climate" "svc_focus" "svc_message_hub" "svc_setup" "lvgl__lvgl" "esp_timer" "heap" "device_control"
)
# 字体超过 LVGL 默认 64 KB 内存池；使用 IDF malloc（大块分配到 PSRAM）。
# 固定配置也覆盖旧 sdkconfig，关闭 UI 后不残留无效的内存文件系统盘符。
target_compile_definitions(__idf_lvgl__lvgl PUBLIC
    LV_USE_STDLIB_MALLOC=LV_STDLIB_CLIB LV_USE_FS_MEMFS=1 LV_FS_MEMFS_LETTER=77)
if(CONFIG_PET_MODULE_UI_ENABLED)
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../../resources/fonts/pet_noto_sc_16.bin" BINARY)
    target_add_binary_data(${COMPONENT_LIB} "${CMAKE_CURRENT_LIST_DIR}/../../../resources/fonts/pet_noto_sc_24.bin" BINARY)
endif()
