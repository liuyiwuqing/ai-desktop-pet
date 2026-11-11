#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#include "pet/net.h"

/* 网络传输 HAL：业务只依赖该结构体；真实/假实现由 svc_net 选择。 */

typedef struct {
    const char *id;
    esp_err_t (*start)(const pet_net_config_t *config);
    esp_err_t (*stop)(void);
    esp_err_t (*publish)(const char *topic, const char *payload, size_t length, bool retain);
    bool (*is_online)(void);
} pet_net_t;

/* ESP-IDF Wi-Fi + esp-mqtt 实现；仅在 CONFIG_PET_NET_FAKE=n 时编入。 */
const pet_net_t *pet_net_esp_driver(void);

/* 确定性假实现；仅在 CONFIG_PET_NET_FAKE=y 时编入。 */
const pet_net_t *pet_fake_net_driver(void);
