#include "pet/hal/climate_decode.h"

pet_climate_error_t pet_climate_decode_dht11(const uint8_t frame[5],
                                             float *temperature_c,
                                             float *humidity_pct)
{
    if (frame == NULL || temperature_c == NULL || humidity_pct == NULL) {
        return PET_CLIMATE_ERR_RANGE;
    }

    const uint8_t checksum = (uint8_t)(frame[0] + frame[1] + frame[2] + frame[3]);
    if (checksum != frame[4]) {
        return PET_CLIMATE_ERR_CHECKSUM;
    }

    const float humidity = (float)frame[0] + (float)frame[1] * 0.1f;
    const float temperature = (float)frame[2] + (float)frame[3] * 0.1f;

    if (temperature < PET_CLIMATE_TEMP_MIN_C || temperature > PET_CLIMATE_TEMP_MAX_C ||
        humidity < PET_CLIMATE_HUMIDITY_MIN_PCT || humidity > PET_CLIMATE_HUMIDITY_MAX_PCT) {
        return PET_CLIMATE_ERR_RANGE;
    }

    *temperature_c = temperature;
    *humidity_pct = humidity;
    return PET_CLIMATE_ERR_NONE;
}
