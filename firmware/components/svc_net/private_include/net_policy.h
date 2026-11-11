#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "pet/net.h"

/* 网络配置与主题策略：纯逻辑，可在主机测试中直接编译。 */

bool pet_net_device_id_valid(const char *device_id);
pet_net_error_t pet_net_config_validate(const pet_net_config_t *config);
bool pet_net_topic_build(pet_net_topic_t topic, const char *device_id,
                         char *out, size_t capacity);
const char *pet_net_state_name(pet_net_state_t state);
const char *pet_net_error_name(pet_net_error_t error);
