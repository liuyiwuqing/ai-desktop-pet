#include "svc_config/api.h"
#include "config_pending.h"

#include <string.h>
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pet/storage.h"

#define HEADER 16u
static struct { SemaphoreHandle_t lock; pet_config_snapshot_t snapshot;uint32_t serial,reservation; } s;
static uint32_t u32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(unsigned char *p, uint32_t x)
{ for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(x >> (i*8)); }
static uint32_t crc(const unsigned char *p, size_t size)
{
    uint32_t c = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        c ^= i >= 12 && i < 16 ? 0 : p[i];
        for (unsigned b = 0; b < 8; ++b) c = (c>>1) ^ (0xEDB88320u & (0u-(c&1u)));
    }
    return c ^ UINT32_MAX;
}
esp_err_t pet_config_init(void)
{
    if (s.lock) return ESP_ERR_INVALID_STATE;
    memset(&s, 0, sizeof(s)); pet_config_defaults(&s.snapshot.config);
    s.lock = xSemaphoreCreateMutex();
    if (!s.lock) return ESP_ERR_NO_MEM;
    unsigned char data[PET_CONFIG_JSON_MAX + HEADER]; size_t size = 0;
    esp_err_t error = pet_storage_init();
    if (error == ESP_OK) error = pet_storage_load("pet_cfg", "settings_v1", data, sizeof(data), &size);
    if (error == ESP_ERR_NVS_NOT_FOUND) { s.snapshot.writable = true; return ESP_OK; }
    if (error != ESP_OK) return error;
    if (size <= HEADER || memcmp(data, "PCFG\1\0", 6) ||
        ((size_t)data[6] | (size_t)data[7]<<8) != size - HEADER || !u32(data+8) ||
        crc(data, size) != u32(data+12) ||
        pet_config_parse_json((const char *)data+HEADER, size-HEADER, &s.snapshot.config) != ESP_OK)
        return ESP_ERR_INVALID_STATE;
    s.snapshot.revision = u32(data+8);
    s.snapshot.writable = s.snapshot.persisted = true;
    return ESP_OK;
}
void pet_config_close(void)
{ if (s.lock) vSemaphoreDelete(s.lock); memset(&s, 0, sizeof(s)); }
esp_err_t pet_config_snapshot(pet_config_snapshot_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s.lock) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s.lock, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    *out = s.snapshot; xSemaphoreGive(s.lock); return ESP_OK;
}
esp_err_t pet_config_reserve(uint32_t expected,uint32_t *lease)
{
    if(!lease)return ESP_ERR_INVALID_ARG;
    if(!s.lock)return ESP_ERR_INVALID_STATE;
    if(xSemaphoreTake(s.lock,0)!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t error=ESP_ERR_INVALID_STATE;
    if(s.reservation)error=ESP_ERR_TIMEOUT;
    else if(s.snapshot.writable&&expected==s.snapshot.revision&&expected!=UINT32_MAX&&s.serial!=UINT32_MAX) {
        *lease=s.reservation=++s.serial;error=ESP_OK;
    }
    xSemaphoreGive(s.lock);return error;
}
void pet_config_cancel_reservation(uint32_t lease)
{
    if(!lease||!s.lock)return;
    xSemaphoreTake(s.lock,portMAX_DELAY);
    if(s.reservation==lease)s.reservation=0;
    xSemaphoreGive(s.lock);
}
static esp_err_t apply(const pet_config_t *config,uint32_t expected,uint32_t lease)
{
    esp_err_t validation=pet_config_validate(config);
    if(validation!=ESP_OK&&!lease)return validation;
    if (!s.lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s.lock, portMAX_DELAY);
    esp_err_t error=validation;
    if(s.reservation!=lease&&(s.reservation||lease)) {error=lease?ESP_ERR_INVALID_STATE:ESP_ERR_TIMEOUT;goto done;}
    if(validation!=ESP_OK)goto done;
    error=ESP_ERR_INVALID_STATE;
    if (!s.snapshot.writable || expected != s.snapshot.revision || expected == UINT32_MAX) goto done;
    unsigned char data[PET_CONFIG_JSON_MAX+HEADER] = "PCFG\1";
    unsigned char check[sizeof(data)];
    size_t bytes, previous_bytes;
    char previous[PET_CONFIG_JSON_MAX+1];
    error = pet_config_format_json(config, (char *)data+HEADER, sizeof(data)-HEADER, &bytes);
    if (error != ESP_OK) goto done;
    error = pet_config_format_json(&s.snapshot.config, previous, sizeof(previous), &previous_bytes);
    if (error != ESP_OK) goto done;
    if (bytes == previous_bytes && !memcmp(data+HEADER, previous, bytes)) { error = ESP_OK; goto done; }
    data[6] = (unsigned char)bytes; data[7] = (unsigned char)(bytes>>8);
    put32(data+8, expected+1); put32(data+12, crc(data, HEADER+bytes));
    error = pet_storage_save("pet_cfg", "settings_v1", data, HEADER+bytes);
    size_t read_bytes = 0;
    esp_err_t read_error = pet_storage_load("pet_cfg", "settings_v1", check, sizeof(check), &read_bytes);
    if (read_error == ESP_OK && read_bytes == HEADER+bytes && !memcmp(data, check, read_bytes)) {
        s.snapshot.config = *config; s.snapshot.revision = expected+1; s.snapshot.persisted = true;
        error = ESP_OK;
    } else if (!(read_error == ESP_ERR_NVS_NOT_FOUND && !s.snapshot.persisted) &&
        !(read_error == ESP_OK && read_bytes > HEADER && u32(check+8) == expected &&
          crc(check, read_bytes) == u32(check+12))) {
        s.snapshot.writable = false;
        error = ESP_ERR_INVALID_STATE;
    } else if (error == ESP_OK) error = ESP_FAIL;
done:
    if(lease&&s.reservation==lease)s.reservation=0;
    xSemaphoreGive(s.lock); return error;
}
esp_err_t pet_config_apply(const pet_config_t *config,uint32_t expected)
{return apply(config,expected,0);}
esp_err_t pet_config_apply_reserved(const pet_config_t *config,uint32_t expected,uint32_t lease)
{return lease?apply(config,expected,lease):ESP_ERR_INVALID_ARG;}
esp_err_t pet_config_apply_json(const char *json, size_t bytes, uint32_t expected)
{
    pet_config_t candidate;
    esp_err_t error = pet_config_parse_json(json, bytes, &candidate);
    return error == ESP_OK ? pet_config_apply(&candidate, expected) : error;
}
esp_err_t pet_config_export_json(char *out, size_t capacity, size_t *bytes)
{
    pet_config_snapshot_t snapshot;
    esp_err_t error = pet_config_snapshot(&snapshot);
    return error == ESP_OK ? pet_config_format_json(&snapshot.config, out, capacity, bytes) : error;
}
