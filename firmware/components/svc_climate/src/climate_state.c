#include "climate_internal.h"

#include <string.h>

uint32_t pet_climate_crc32(const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;

    for (size_t i = 0; i < size; i++) {
        crc ^= bytes[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

void pet_climate_state_defaults(pet_climate_state_blob_t *state)
{
    if (state == NULL) {
        return;
    }
    *state = (pet_climate_state_blob_t){
        .magic = PET_CLIMATE_STATE_MAGIC,
        .version = PET_CLIMATE_STATE_VERSION,
        .length = (uint16_t)sizeof(pet_climate_state_blob_t),
        .day_index = UINT32_MAX,
        .temperature_min_centi = INT16_MAX,
        .temperature_max_centi = INT16_MIN,
        .humidity_min_centi = INT16_MAX,
        .humidity_max_centi = INT16_MIN,
        .temperature_offset_centi = 0,
        .humidity_offset_centi = 0,
    };
}

static int16_t to_centi(float value)
{
    return (int16_t)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
}

void pet_climate_state_update(pet_climate_state_blob_t *state, const pet_climate_reading_t *reading)
{
    if (state == NULL || reading == NULL || !reading->valid) {
        return;
    }

    const uint32_t day_index = reading->timestamp_ms / PET_CLIMATE_MS_PER_DAY;
    if (state->day_index != day_index) {
        state->day_index = day_index;
        state->temperature_min_centi = INT16_MAX;
        state->temperature_max_centi = INT16_MIN;
        state->humidity_min_centi = INT16_MAX;
        state->humidity_max_centi = INT16_MIN;
    }

    const int16_t temperature_centi = to_centi(reading->temperature_c);
    const int16_t humidity_centi = to_centi(reading->humidity_pct);

    if (temperature_centi < state->temperature_min_centi) {
        state->temperature_min_centi = temperature_centi;
    }
    if (temperature_centi > state->temperature_max_centi) {
        state->temperature_max_centi = temperature_centi;
    }
    if (humidity_centi < state->humidity_min_centi) {
        state->humidity_min_centi = humidity_centi;
    }
    if (humidity_centi > state->humidity_max_centi) {
        state->humidity_max_centi = humidity_centi;
    }
}

bool pet_climate_state_encode(const pet_climate_state_blob_t *state, void *out,
                              size_t capacity, size_t *out_size)
{
    if (state == NULL || out == NULL || out_size == NULL ||
        capacity < sizeof(pet_climate_state_blob_t)) {
        return false;
    }

    pet_climate_state_blob_t blob = *state;
    blob.magic = PET_CLIMATE_STATE_MAGIC;
    blob.version = PET_CLIMATE_STATE_VERSION;
    blob.length = (uint16_t)sizeof(pet_climate_state_blob_t);
    blob.crc32 = 0;
    blob.crc32 = pet_climate_crc32(&blob, sizeof(blob));

    memcpy(out, &blob, sizeof(blob));
    *out_size = sizeof(blob);
    return true;
}

bool pet_climate_state_decode(const void *data, size_t size, pet_climate_state_blob_t *out)
{
    if (data == NULL || out == NULL || size != sizeof(pet_climate_state_blob_t)) {
        return false;
    }

    pet_climate_state_blob_t blob;
    memcpy(&blob, data, sizeof(blob));

    if (blob.magic != PET_CLIMATE_STATE_MAGIC || blob.version != PET_CLIMATE_STATE_VERSION ||
        blob.length != sizeof(pet_climate_state_blob_t)) {
        return false;
    }

    const uint32_t expected_crc = blob.crc32;
    blob.crc32 = 0;
    if (pet_climate_crc32(&blob, sizeof(blob)) != expected_crc) {
        return false;
    }

    blob.crc32 = expected_crc;
    *out = blob;
    return true;
}
