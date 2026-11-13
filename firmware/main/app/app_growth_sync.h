#pragma once
#include <stdint.h>
void app_growth_sync_init(const char *device_id);
void app_growth_sync_poll(uint32_t epoch);
