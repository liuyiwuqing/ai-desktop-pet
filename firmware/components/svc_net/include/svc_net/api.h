#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "pet/hal/net.h"
#include "pet/net.h"

/* 网络服务：配置校验、驱动选择与生命周期；真实/假传输由 Kconfig 决定。 */

esp_err_t pet_net_init(const pet_net_config_t *config);
esp_err_t pet_net_start(void);
esp_err_t pet_net_stop(void);
bool pet_net_is_online(void);

/* 注册收包回调；应在 pet_net_start 前调用。 */
esp_err_t pet_net_set_rx_handler(pet_net_rx_handler_t handler, void *context);

/* 使用已保存的 device_id 构建主题并发布。 */
esp_err_t pet_net_publish_topic(pet_net_topic_t topic, const char *payload,
                                size_t length, bool retain);

/* SNTP 是否已成功对时；未对时时消息 TTL 检查会降级。 */
bool pet_net_is_time_synced(void);

const char *pet_net_state_name(pet_net_state_t state);
const char *pet_net_error_name(pet_net_error_t error);
