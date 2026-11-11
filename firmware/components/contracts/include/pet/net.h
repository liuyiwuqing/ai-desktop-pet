#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet/event.h"

/* 网络传输契约：Wi-Fi/MQTT 的配置与状态，不包含具体驱动实现。 */

#define PET_NET_DEVICE_ID_MAX 31
#define PET_NET_URI_MAX 127
#define PET_NET_SSID_MAX 32
#define PET_NET_PASSWORD_MAX 64
#define PET_NET_USERNAME_MAX 64
#define PET_NET_TOKEN_MAX 128
#define PET_NET_CA_PEM_MAX 3000
#define PET_NET_TOPIC_MAX 96

typedef enum {
    PET_NET_TOPIC_INBOX = 0,
    PET_NET_TOPIC_ACK,
    PET_NET_TOPIC_STATUS,
    PET_NET_TOPIC_CONFIG,
    PET_NET_TOPIC_COUNT,
} pet_net_topic_t;

typedef enum {
    PET_NET_STATE_IDLE = 0,
    PET_NET_STATE_CONNECTING,
    PET_NET_STATE_ONLINE,
    PET_NET_STATE_RECONNECTING,
    PET_NET_STATE_OFFLINE,
    PET_NET_STATE_COUNT,
} pet_net_state_t;

typedef enum {
    PET_NET_ERROR_NONE = 0,
    PET_NET_ERROR_CONFIG,
    PET_NET_ERROR_WIFI,
    PET_NET_ERROR_TLS,
    PET_NET_ERROR_MQTT,
    PET_NET_ERROR_COUNT,
} pet_net_error_t;

/* 配置字符串由调用方持有，生命周期需覆盖整个网络运行期。 */
typedef struct {
    const char *wifi_ssid;
    const char *wifi_password;
    const char *broker_uri; /* 仅接受 mqtts://，TLS 不可关闭 */
    const char *device_id;
    const char *mqtt_username;
    const char *mqtt_password;
    const char *ca_cert_pem; /* NULL 时使用 ESP-IDF 证书包 */
    uint16_t keepalive_s;
    uint32_t reconnect_timeout_ms;
} pet_net_config_t;

typedef struct {
    pet_net_state_t state;
    pet_net_error_t error;
    uint16_t reconnect_count;
    uint8_t reserved[2];
} pet_net_event_t;

_Static_assert(sizeof(pet_net_event_t) <= PET_EVENT_PAYLOAD_MAX,
               "pet_net_event_t must fit the event payload");
