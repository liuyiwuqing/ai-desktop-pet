#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "climate_internal.h"

static pet_climate_reading_t reading(float temperature_c, float humidity_pct, uint32_t timestamp_ms)
{
    const pet_climate_reading_t value = {
        .temperature_c = temperature_c,
        .humidity_pct = humidity_pct,
        .timestamp_ms = timestamp_ms,
        .error_code = PET_CLIMATE_ERR_NONE,
        .quality = 100,
        .valid = true,
    };
    return value;
}

static void test_roundtrip(void)
{
    pet_climate_state_blob_t state;
    pet_climate_state_defaults(&state);

    const pet_climate_reading_t first = reading(24.0f, 50.0f, 1000);
    pet_climate_state_update(&state, &first);

    uint8_t buffer[64];
    size_t size = 0;
    assert(pet_climate_state_encode(&state, buffer, sizeof(buffer), &size));
    assert(size == sizeof(pet_climate_state_blob_t));

    pet_climate_state_blob_t decoded;
    assert(pet_climate_state_decode(buffer, size, &decoded));
    assert(decoded.temperature_min_centi == 2400);
    assert(decoded.temperature_max_centi == 2400);
    assert(decoded.humidity_min_centi == 5000);
    assert(decoded.humidity_max_centi == 5000);
    assert(decoded.day_index == 0);
}

static void test_min_max_and_day_rollover(void)
{
    pet_climate_state_blob_t state;
    pet_climate_state_defaults(&state);

    pet_climate_reading_t value = reading(24.0f, 50.0f, 1000);
    pet_climate_state_update(&state, &value);
    value = reading(20.0f, 40.0f, 2000);
    pet_climate_state_update(&state, &value);
    value = reading(26.0f, 70.0f, 3000);
    pet_climate_state_update(&state, &value);

    assert(state.temperature_min_centi == 2000);
    assert(state.temperature_max_centi == 2600);
    assert(state.humidity_min_centi == 4000);
    assert(state.humidity_max_centi == 7000);

    value = reading(30.0f, 60.0f, PET_CLIMATE_MS_PER_DAY + 1000);
    pet_climate_state_update(&state, &value);
    assert(state.day_index == 1);
    assert(state.temperature_min_centi == 3000);
    assert(state.temperature_max_centi == 3000);
    assert(state.humidity_min_centi == 6000);
    assert(state.humidity_max_centi == 6000);
}

static void test_invalid_reading_ignored(void)
{
    pet_climate_state_blob_t state;
    pet_climate_state_defaults(&state);

    pet_climate_reading_t value = reading(24.0f, 50.0f, 1000);
    pet_climate_state_update(&state, &value);

    value.valid = false;
    value.temperature_c = 40.0f;
    pet_climate_state_update(&state, &value);

    assert(state.temperature_max_centi == 2400);
}

static void test_corruption_rejected(void)
{
    pet_climate_state_blob_t state;
    pet_climate_state_defaults(&state);

    uint8_t buffer[64];
    size_t size = 0;
    assert(pet_climate_state_encode(&state, buffer, sizeof(buffer), &size));

    pet_climate_state_blob_t decoded;
    buffer[8] ^= 0x01u;
    assert(!pet_climate_state_decode(buffer, size, &decoded));

    assert(pet_climate_state_encode(&state, buffer, sizeof(buffer), &size));
    pet_climate_state_blob_t forged;
    memcpy(&forged, buffer, sizeof(forged));
    forged.magic = 0;
    assert(!pet_climate_state_decode(&forged, sizeof(forged), &decoded));

    assert(pet_climate_state_encode(&state, buffer, sizeof(buffer), &size));
    memcpy(&forged, buffer, sizeof(forged));
    forged.version = 99;
    assert(!pet_climate_state_decode(&forged, sizeof(forged), &decoded));

    assert(!pet_climate_state_decode(buffer, size - 1, &decoded));
}

static void test_crc_known_vector(void)
{
    assert(pet_climate_crc32("123456789", 9) == 0xCBF43926u);
}

int main(void)
{
    test_roundtrip();
    test_min_max_and_day_rollover();
    test_invalid_reading_ignored();
    test_corruption_rejected();
    test_crc_known_vector();
    printf("test_climate_state: ok\n");
    return 0;
}
