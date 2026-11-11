#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "pet/net.h"

/* 网络服务：配置校验、驱动选择与生命周期；真实/假传输由 Kconfig 决定。 */

esp_err_t pet_net_init(const pet_net_config_t *config);
esp_err_t pet_net_start(void);
esp_err_t pet_net_stop(void);
bool pet_net_is_online(void);
const char *pet_net_state_name(pet_net_state_t state);
const char *pet_net_error_name(pet_net_error_t error);
