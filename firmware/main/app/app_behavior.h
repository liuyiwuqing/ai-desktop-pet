#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t app_behavior_start(void);
void app_behavior_dialogue(uint32_t turn_id);
esp_err_t app_focus_command(const char *action);
