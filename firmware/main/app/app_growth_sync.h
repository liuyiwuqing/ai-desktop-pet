#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
void app_growth_sync_init(const char *device_id);
void app_growth_sync_poll(uint32_t epoch);

bool app_companion_message(const char *topic,const char *payload,size_t bytes,bool retained,uint32_t epoch);
