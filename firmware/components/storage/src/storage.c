#include "pet/storage.h"

#include <string.h>
#include <stdatomic.h>
static _Atomic uint32_t write_attempts,write_failures,remove_attempts;

#include "nvs.h"
#include "nvs_flash.h"

esp_err_t pet_storage_init(void)
{
    /* 同一分区同时含凭据、存档和收件箱；初始化失败交给上层降级，禁止自动擦除。 */
    return nvs_flash_init();
}

esp_err_t pet_storage_load(const char *namespace_name, const char *key,
                           void *out, size_t capacity, size_t *out_size)
{
    if (namespace_name == NULL || key == NULL || out == NULL || out_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t length = capacity;
    err = nvs_get_blob(handle, key, out, &length);
    nvs_close(handle);
    if (err == ESP_OK) {
        *out_size = length;
    }
    return err;
}

static esp_err_t save_blob(const char *namespace_name, const char *key,
                           const void *data, size_t size)
{
    if (namespace_name == NULL || key == NULL || data == NULL || size == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(handle, key, data, size);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t pet_storage_load_str(const char *namespace_name, const char *key,
                               char *out, size_t capacity)
{
    if (namespace_name == NULL || key == NULL || out == NULL || capacity == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    size_t length = capacity;
    err = nvs_get_str(handle, key, out, &length);
    nvs_close(handle);
    return err;
}

esp_err_t pet_storage_remove(const char *namespace_name, const char *key)
{
    atomic_fetch_add(&remove_attempts,1);
    if (namespace_name == NULL || key == NULL)
        return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK)
        return err;
    err = nvs_erase_key(handle, key);
    if (err == ESP_ERR_NVS_NOT_FOUND)
        err = ESP_OK;
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

static esp_err_t save_string(const char *namespace_name, const char *key, const char *value)
{
    if (namespace_name == NULL || key == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(namespace_name, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, key, value);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

esp_err_t pet_storage_save(const char *ns,const char *key,const void *data,size_t size)
{
    atomic_fetch_add(&write_attempts,1);esp_err_t error=save_blob(ns,key,data,size);
    if (error!=ESP_OK) atomic_fetch_add(&write_failures,1);
    return error;
}
esp_err_t pet_storage_save_str(const char *ns,const char *key,const char *value)
{
    atomic_fetch_add(&write_attempts,1);esp_err_t error=save_string(ns,key,value);
    if (error!=ESP_OK) atomic_fetch_add(&write_failures,1);
    return error;
}
void pet_storage_metrics(pet_storage_metrics_t *out)
{
    if (out) *out=(pet_storage_metrics_t){atomic_load(&write_attempts),atomic_load(&write_failures),atomic_load(&remove_attempts)};
}
