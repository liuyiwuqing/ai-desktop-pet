#pragma once
#include <stddef.h>
#include "esp_err.h"
esp_err_t app_backup_command(const char *line,size_t bytes,char *out,size_t capacity);
