#include "pet/hal/display.h"
#include "sdkconfig.h"
#if CONFIG_PET_EXT_LIGHT_SENSOR
#include "board_generated.h"
#include "bsp_analog.h"

esp_err_t pet_bsp_ambient_read(uint16_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    int raw;
    esp_err_t error = pet_bsp_analog_read(PET_BOARD_LIGHT_GPIO, &raw);
    if (error != ESP_OK) return error;
    if (raw < 0 || raw > 4095) return ESP_ERR_INVALID_ARG;
#if CONFIG_PET_LIGHT_SENSOR_INVERT
    raw = 4095 - raw;
#endif
    *out = raw;
    return ESP_OK;
}
#endif
