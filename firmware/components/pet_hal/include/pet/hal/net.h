#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "pet/net.h"

/* 网络传输 HAL：业务只依赖该结构体；真实/假实现由 svc_net 选择。 */

/* 收到完整 MQTT 应用消息时回调；topic/payload 仅在回调期间有效。 */
typedef void (*pet_net_rx_handler_t)(const char *topic, const char *payload,
                                     size_t length, bool retained, void *context);

typedef struct {
    const char *id;
    esp_err_t (*start)(const pet_net_config_t *config);
    esp_err_t (*stop)(void);
    esp_err_t (*publish)(const char *topic, const char *payload, size_t length, bool retain);
    /* 从非 MQTT 任务调用；只有匹配的 QoS 1 PUBACK 才返回 ESP_OK。 */
    esp_err_t (*publish_confirmed)(const char *topic, const char *payload, size_t length,
                                   bool retain, uint32_t timeout_ms);
    esp_err_t (*set_rx_handler)(pet_net_rx_handler_t handler, void *context);
    bool (*is_online)(void);
    bool (*is_time_synced)(void);
} pet_net_t;

/* ESP-IDF Wi-Fi + esp-mqtt 实现；仅在 CONFIG_PET_NET_FAKE=n 时编入。 */
const pet_net_t *pet_net_esp_driver(void);

/* 确定性假实现；仅在 CONFIG_PET_NET_FAKE=y 时编入。 */
const pet_net_t *pet_fake_net_driver(void);
