#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t app_diagnostics_json(char *out,size_t capacity);
esp_err_t app_summary_json(const char *device,uint32_t epoch,char *out,size_t capacity);

esp_err_t app_maintenance_command(const char *line,size_t bytes,char *out,size_t capacity);
