#include "sdkconfig.h"

#if CONFIG_PET_CLIMATE_FAKE_SENSOR

#include "pet/hal/climate_sensor.h"

/* 无硬件时的确定性假传感器：固定序列，便于台面联调与串口诊断。 */
static uint32_t s_sample;

static esp_err_t fake_init(void)
{
    s_sample = 0;
    return ESP_OK;
}

static esp_err_t fake_read(pet_climate_reading_t *out)
{
    static const float temperatures[] = {24.0f, 24.2f, 24.4f, 24.6f, 24.4f, 24.2f};
    static const float humidities[] = {55.0f, 56.0f, 57.0f, 58.0f, 57.0f, 56.0f};

    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t index = s_sample % (sizeof(temperatures) / sizeof(temperatures[0]));
    out->temperature_c = temperatures[index];
    out->humidity_pct = humidities[index];
    out->timestamp_ms = 0;
    out->error_code = PET_CLIMATE_ERR_NONE;
    out->quality = 100;
    out->valid = true;

    s_sample++;
    return ESP_OK;
}

static const pet_climate_sensor_t s_fake_sensor = {
    .id = "fake-dht11",
    .init = fake_init,
    .read = fake_read,
};

const pet_climate_sensor_t *pet_fake_climate_sensor(void)
{
    return &s_fake_sensor;
}

#endif
