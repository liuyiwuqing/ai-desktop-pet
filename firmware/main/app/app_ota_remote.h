#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void app_ota_remote_init(const char *device);
bool app_ota_remote_message(const char *topic,const char *payload,size_t bytes,bool retained,uint32_t now);
void app_ota_remote_poll(uint32_t now);
