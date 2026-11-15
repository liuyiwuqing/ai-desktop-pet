#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PET_SETUP_WINDOW_S 900u
#define PET_SETUP_AP_MAX 20u
#define PET_SETUP_HOST "192.168.4.1"
#define PET_SETUP_ORIGIN "http://192.168.4.1"

typedef enum {
    PET_SETUP_STARTING, PET_SETUP_READY, PET_SETUP_CONNECTING, PET_SETUP_WIFI_SAVED,
    PET_SETUP_ACTIVATING, PET_SETUP_WAITING_BIND, PET_SETUP_VOICE_SAVED,
    PET_SETUP_SERVICES_SAVED, PET_SETUP_FAILED, PET_SETUP_LEAVING
} pet_setup_state_t;

typedef struct {
    pet_setup_state_t state;
    esp_err_t error;
    uint32_t revision, remaining_s;
    bool wifi_saved, voice_saved, nexus_saved;
    char ap_ssid[33], ap_password[13], activation_code[17];
} pet_setup_snapshot_t;

/* 专用启动模式独占 Wi-Fi/HTTP；正常语音/消息服务启动前由组合根调用。 */
esp_err_t pet_setup_should_start(bool *start);
esp_err_t pet_setup_start(void);
esp_err_t pet_setup_snapshot(pet_setup_snapshot_t *out);
/* UI 只排队，不在 LVGL 线程执行 Flash 写入或重启。 */
esp_err_t pet_setup_request(void);
esp_err_t pet_setup_leave(void);
bool pet_setup_request_failed(void);

/* 下列入口仅交给绑定 SoftAP 接口、校验会话的 HTTP 适配器。 */
esp_err_t pet_setup_submit_wifi(const char *ssid, const char *password, uint32_t revision);
esp_err_t pet_setup_submit_services(const char *json, size_t bytes, uint32_t revision);
esp_err_t pet_setup_activate(void);
