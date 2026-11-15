#include "sdkconfig.h"
#if CONFIG_PET_EXT_JOYSTICK || CONFIG_PET_EXT_LIGHT_SENSOR
#include "bsp_analog.h"
#include "esp_adc/adc_oneshot.h"

static adc_oneshot_unit_handle_t s_adc;
static unsigned s_channels;

esp_err_t pet_bsp_analog_read(int gpio, int *raw)
{
    if (!raw) return ESP_ERR_INVALID_ARG;
    adc_unit_t unit;
    adc_channel_t channel;
    esp_err_t error = adc_oneshot_io_to_channel(gpio, &unit, &channel);
    if (error != ESP_OK || unit != ADC_UNIT_1) return ESP_ERR_INVALID_ARG;
    if (!s_adc) {
        const adc_oneshot_unit_init_cfg_t config = {.unit_id = ADC_UNIT_1};
        error = adc_oneshot_new_unit(&config, &s_adc);
        if (error != ESP_OK) return error;
    }
    if (!(s_channels & (1u << channel))) {
        const adc_oneshot_chan_cfg_t config = {.atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12};
        error = adc_oneshot_config_channel(s_adc, channel, &config);
        if (error != ESP_OK) return error;
        s_channels |= 1u << channel;
    }
    return adc_oneshot_read(s_adc, channel, raw);
}
#endif
