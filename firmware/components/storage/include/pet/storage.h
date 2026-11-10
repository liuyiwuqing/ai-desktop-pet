#pragma once

#include <stddef.h>

#include "esp_err.h"

/* NVS 薄封装：模块只关心键值 blob，不直接依赖 nvs 句柄。 */
esp_err_t pet_storage_init(void);
esp_err_t pet_storage_load(const char *namespace_name, const char *key,
                           void *out, size_t capacity, size_t *out_size);
esp_err_t pet_storage_save(const char *namespace_name, const char *key,
                           const void *data, size_t size);
