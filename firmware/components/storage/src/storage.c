#include "pet/storage.h"

#include <string.h>

#include "nvs.h"
#include "nvs_flash.h"

esp_err_t pet_storage_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* 首版允许清空重建；凭据命名空间由配网流程单独管理。 */
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    return err;
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

esp_err_t pet_storage_save(const char *namespace_name, const char *key,
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

esp_err_t pet_storage_save_str(const char *namespace_name, const char *key,
                               const char *value)
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
