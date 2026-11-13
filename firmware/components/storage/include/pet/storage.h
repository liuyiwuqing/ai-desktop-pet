#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

/* NVS 薄封装：模块只关心键值（blob/字符串），不直接依赖 nvs 句柄。 */
esp_err_t pet_storage_init(void);
esp_err_t pet_storage_load(const char *namespace_name, const char *key, void *out, size_t capacity,
                           size_t *out_size);
esp_err_t pet_storage_save(const char *namespace_name, const char *key, const void *data,
                           size_t size);
/* 幂等删除指定键；仅用于已有恢复副本的非活动数据，不擦除分区。 */
esp_err_t pet_storage_remove(const char *namespace_name, const char *key);
esp_err_t pet_storage_load_str(const char *namespace_name, const char *key, char *out,
                               size_t capacity);
esp_err_t pet_storage_save_str(const char *namespace_name, const char *key, const char *value);

typedef struct {uint32_t write_attempts,write_failures,remove_attempts;} pet_storage_metrics_t;
void pet_storage_metrics(pet_storage_metrics_t *out);
