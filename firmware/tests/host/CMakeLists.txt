cmake_minimum_required(VERSION 3.18)
project(pet_ui_host LANGUAGES C CXX)
set(CMAKE_C_STANDARD 17)
get_filename_component(PET_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory("${PET_ROOT}/firmware/managed_components/lvgl__lvgl" lvgl)
add_executable(test_message_view test_message_view.c
    "${PET_ROOT}/firmware/components/ui_lvgl/src/message_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/growth_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/ai_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/climate_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/control_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/recovery_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/setup_view.c"
    "${PET_ROOT}/firmware/components/presentation/src/climate_card.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/pet_avatar.c"
    "${PET_ROOT}/firmware/components/presentation/src/pet_motion.c")
target_include_directories(test_message_view PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/support"
    "${PET_ROOT}/firmware/components/svc_setup/include"
    "${PET_ROOT}/firmware/components/pet_hal/include"
    "${PET_ROOT}/firmware/components/contracts/include"
    "${PET_ROOT}/firmware/components/presentation/include"
    "${PET_ROOT}/firmware/components/ui_lvgl/src")
target_link_libraries(test_message_view PRIVATE lvgl)
target_compile_options(test_message_view PRIVATE -Wall -Wextra -Werror)
enable_testing()
add_test(NAME message_view COMMAND test_message_view "${PET_ROOT}/resources/fonts" "${CMAKE_BINARY_DIR}")
add_executable(test_pet_avatar test_pet_avatar.c
    "${PET_ROOT}/firmware/components/ui_lvgl/src/pet_avatar.c"
    "${PET_ROOT}/firmware/components/presentation/src/pet_motion.c")
target_include_directories(test_pet_avatar PRIVATE
    "${PET_ROOT}/firmware/components/contracts/include"
    "${PET_ROOT}/firmware/components/presentation/include"
    "${PET_ROOT}/firmware/components/ui_lvgl/src")
target_link_libraries(test_pet_avatar PRIVATE lvgl)
target_compile_options(test_pet_avatar PRIVATE -Wall -Wextra -Werror)
add_test(NAME pet_avatar COMMAND test_pet_avatar "${CMAKE_BINARY_DIR}")
add_executable(test_recovery_ui test_recovery_ui.c
    "${PET_ROOT}/firmware/components/ui_lvgl/src/recovery_ui.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/recovery_view.c"
    "${PET_ROOT}/firmware/components/ui_lvgl/src/ui_owner.c"
    "${PET_ROOT}/firmware/components/presentation/src/recovery_presenter.c")
target_include_directories(test_recovery_ui PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/support/ai_agent" "${CMAKE_CURRENT_LIST_DIR}/support"
    "${PET_ROOT}/firmware/components/ui_lvgl/include"
    "${PET_ROOT}/firmware/components/ui_lvgl/src"
    "${PET_ROOT}/firmware/components/pet_hal/include"
    "${PET_ROOT}/firmware/components/presentation/include")
target_compile_definitions(test_recovery_ui PRIVATE PET_FONT_DIR="${PET_ROOT}/resources/fonts")
target_compile_options(test_recovery_ui PRIVATE -Wall -Wextra -Werror)
target_link_libraries(test_recovery_ui PRIVATE lvgl)
foreach(scenario ok init flush flush-final backlight-final no-button allocation task)
    add_test(NAME recovery_ui_${scenario} COMMAND test_recovery_ui ${scenario})
endforeach()
