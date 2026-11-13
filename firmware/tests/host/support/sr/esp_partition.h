#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct { size_t size; } esp_partition_t;
typedef uint32_t esp_partition_mmap_handle_t;
typedef int esp_partition_type_t;
typedef int esp_partition_subtype_t;
typedef int esp_partition_mmap_flag_t;
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 0x82
#define ESP_PARTITION_MMAP_DATA 0
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
    esp_partition_subtype_t subtype, const char *label);
esp_err_t esp_partition_mmap(const esp_partition_t *partition, size_t offset, size_t size,
    esp_partition_mmap_flag_t flags, const void **out, esp_partition_mmap_handle_t *handle);
void esp_partition_munmap(esp_partition_mmap_handle_t handle);
