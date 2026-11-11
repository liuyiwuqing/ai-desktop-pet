#include "message_store.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "nvs.h"

#include "pet/storage.h"

#include "message_store_codec.h"

static const char *TAG = "msg_store";

#define STORE_NAMESPACE "msg_inbox"
#define STORE_KEY_ACTIVE "active"
#define STORE_KEY_SNAP0 "snap0"
#define STORE_KEY_SNAP1 "snap1"

static uint8_t *s_buffer;

static const char *generation_key(uint8_t generation)
{
    return generation == 0 ? STORE_KEY_SNAP0 : STORE_KEY_SNAP1;
}

esp_err_t pet_message_store_init(void)
{
    if (s_buffer != NULL) {
        return ESP_OK;
    }

    esp_err_t err = pet_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_buffer = heap_caps_malloc(PET_MESSAGE_STORE_MAX_SIZE,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_buffer == NULL) {
        s_buffer = heap_caps_malloc(PET_MESSAGE_STORE_MAX_SIZE,
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_buffer == NULL) {
        ESP_LOGE(TAG, "snapshot buffer alloc failed: %u B",
                 (unsigned)PET_MESSAGE_STORE_MAX_SIZE);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t pet_message_store_load(pet_message_snapshot_t *out)
{
    if (out == NULL || s_buffer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));

    uint8_t active = 0;
    size_t active_length = 0;
    esp_err_t err = pet_storage_load(STORE_NAMESPACE, STORE_KEY_ACTIVE, &active,
                                     sizeof(active), &active_length);
    if (err != ESP_OK || active_length != sizeof(active) || active > 1) {
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "invalid active generation record");
            return ESP_ERR_INVALID_STATE;
        }
        return err == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : err;
    }

    for (uint8_t attempt = 0; attempt < 2; attempt++) {
        const uint8_t generation = (uint8_t)((active + attempt) & 1u);
        const char *key = generation_key(generation);
        size_t length = 0;
        err = pet_storage_load(STORE_NAMESPACE, key, s_buffer, PET_MESSAGE_STORE_MAX_SIZE,
                               &length);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s read failed: %s", key, esp_err_to_name(err));
            continue;
        }

        uint8_t count = 0;
        uint8_t dedupe_next = 0;
        const pet_message_store_error_t decoded = pet_message_store_decode(
            s_buffer, length, out->messages, PET_MESSAGE_QUEUE_CAPACITY, &count,
            out->dedupe, &dedupe_next);
        if (decoded != PET_MESSAGE_STORE_OK) {
            ESP_LOGW(TAG, "%s decode failed: %d", key, (int)decoded);
            continue;
        }

        out->count = count;
        out->dedupe_next = dedupe_next;
        ESP_LOGI(TAG, "loaded %s: %u messages, %u B", key, count, (unsigned)length);
        return ESP_OK;
    }

    ESP_LOGW(TAG, "both snapshot generations unusable");
    return ESP_ERR_INVALID_STATE;
}

esp_err_t pet_message_store_save(const pet_message_snapshot_t *snapshot)
{
    if (snapshot == NULL || s_buffer == NULL || snapshot->count > PET_MESSAGE_QUEUE_CAPACITY ||
        snapshot->dedupe_next >= PET_MESSAGE_DEDUPE_SLOTS) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t length = 0;
    const pet_message_store_error_t encoded = pet_message_store_encode(
        snapshot->messages, snapshot->count, snapshot->dedupe, snapshot->dedupe_next,
        s_buffer, PET_MESSAGE_STORE_MAX_SIZE, &length);
    if (encoded != PET_MESSAGE_STORE_OK) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t active = 0;
    size_t active_length = 0;
    const esp_err_t active_err = pet_storage_load(STORE_NAMESPACE, STORE_KEY_ACTIVE, &active,
                                                  sizeof(active), &active_length);
    const uint8_t next =
        (active_err == ESP_OK && active_length == sizeof(active) && active <= 1)
            ? (uint8_t)(active ^ 1u)
            : 0u;

    esp_err_t err = pet_storage_save(STORE_NAMESPACE, generation_key(next), s_buffer, length);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "write %s failed: %s", generation_key(next), esp_err_to_name(err));
        return err;
    }

    err = pet_storage_save(STORE_NAMESPACE, STORE_KEY_ACTIVE, &next, sizeof(next));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "switch active failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGD(TAG, "saved %s: %u messages, %u B", generation_key(next), snapshot->count,
             (unsigned)length);
    return ESP_OK;
}
