#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "pet/netcfg.h"

#define PET_SETUP_WINDOW_S 900u
#define PET_SETUP_WINDOW_MIN_S 300u
#define PET_SETUP_WINDOW_MAX_S 3600u
#define PET_SETUP_AP_MAX 20u
#define PET_SETUP_SSID_MAX 32u
#define PET_SETUP_PASSWORD_MAX 63u
#define PET_SETUP_CHANNEL_MIN 1u
#define PET_SETUP_CHANNEL_MAX 13u
#define PET_SETUP_CLIENTS_MIN 1u
#define PET_SETUP_CLIENTS_MAX 4u
#define PET_SETUP_HOST "192.168.4.1"
#define PET_SETUP_ORIGIN "http://192.168.4.1"

typedef enum {
    PET_SETUP_STARTING, PET_SETUP_READY, PET_SETUP_CONNECTING, PET_SETUP_WIFI_SAVED,
    PET_SETUP_ACTIVATING, PET_SETUP_WAITING_BIND, PET_SETUP_VOICE_SAVED,
    PET_SETUP_SERVICES_SAVED, PET_SETUP_PORTAL_SAVED, PET_SETUP_NETWORK_SAVED, PET_SETUP_FAILED,
    PET_SETUP_LEAVING
} pet_setup_state_t;

/* 设置热点密码策略：每次随机、固定口令、开放网络。自定义口令只存 NVS，不回显。 */
typedef enum {
    PET_SETUP_PASSWORD_AUTO = 0, PET_SETUP_PASSWORD_CUSTOM = 1, PET_SETUP_PASSWORD_OPEN = 2
} pet_setup_password_mode_t;

/* 设备热点偏好。custom=false 时每次进入重新生成 Qitomo-XXXX 名称。 */
typedef struct {
    bool custom;
    char ssid[PET_SETUP_SSID_MAX + 1];
    pet_setup_password_mode_t password_mode;
    char password[PET_SETUP_PASSWORD_MAX + 1];
    bool hidden;
    uint8_t channel;
    uint8_t max_clients;
    uint16_t window_s;
} pet_setup_portal_t;

/* 家庭网络地址：与正常模式共用 net 凭据组，ip_mode 为空表示 DHCP。 */
typedef struct {
    char ip_mode[8];
    char ip[PET_NETCFG_IP_MAX + 1];
    char netmask[PET_NETCFG_IP_MAX + 1];
    char gateway[PET_NETCFG_IP_MAX + 1];
    char dns[PET_NETCFG_IP_MAX + 1];
    char hostname[PET_NETCFG_HOSTNAME_MAX + 1];
} pet_setup_netcfg_t;

typedef struct {
    pet_setup_state_t state;
    esp_err_t error;
    uint32_t revision, remaining_s;
    bool wifi_saved, voice_saved, nexus_saved;
    bool ap_hidden; /* 当前热点的实际状态，可能落后于已保存偏好。 */
    char ap_ssid[PET_SETUP_SSID_MAX + 1], ap_password[PET_SETUP_PASSWORD_MAX + 1], activation_code[17];
    char wifi_ssid[PET_SETUP_SSID_MAX + 1];
    pet_setup_portal_t portal; /* 已保存的偏好，不代表当前热点已经重开。 */
    pet_setup_netcfg_t netcfg;
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
/* restart=true 时保存后以设置模式重启，让新热点立即生效。 */
esp_err_t pet_setup_submit_portal(const pet_setup_portal_t *portal, bool restart, uint32_t revision);
/* 固定地址只在已保存家庭 Wi-Fi 后写入同一 net 分组，保留 SSID/口令。 */
esp_err_t pet_setup_submit_network(const pet_setup_netcfg_t *netcfg, uint32_t revision);
