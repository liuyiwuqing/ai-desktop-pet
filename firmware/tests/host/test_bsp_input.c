#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "pet/hal/input.h"
#include "pet/hal/display.h"
#include "board_generated.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

static bool fail_create = true, fail_read, fail_channel;
static int levels[49], raw_x = 2048, raw_y = 2048, raw_light = 1200, units;
esp_err_t gpio_config(const gpio_config_t *config)
{
    assert(config->mode == GPIO_MODE_INPUT && config->pull_up_en == GPIO_PULLUP_ENABLE);
    const uint64_t keys_mask = (1ULL << PET_BOARD_KEY_CONFIRM_GPIO) | (1ULL << PET_BOARD_KEY_NEXT_GPIO) |
        (1ULL << PET_BOARD_KEY_VOICE_GPIO);
    assert(config->pin_bit_mask == 1ULL || config->pin_bit_mask == (1ULL << 6) ||
        config->pin_bit_mask == keys_mask);
    return ESP_OK;
}
int gpio_get_level(int pin) { return levels[pin]; }
esp_err_t adc_oneshot_io_to_channel(int pin, adc_unit_t *unit, adc_channel_t *channel)
{
    *unit = ADC_UNIT_1; *channel = pin - 1;
    return pin >= 1 && pin <= 10 ? ESP_OK : ESP_ERR_INVALID_ARG;
}
esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *config, adc_oneshot_unit_handle_t *unit)
{
    assert(config->unit_id == ADC_UNIT_1);
    if (fail_create) return ESP_ERR_NO_MEM;
    ++units; *unit = &units; return ESP_OK;
}
esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t unit, adc_channel_t channel,
                                    const adc_oneshot_chan_cfg_t *config)
{
    assert(unit && (channel == 3 || channel == 4 || channel == 6));
    assert(config->atten == ADC_ATTEN_DB_12 && config->bitwidth == ADC_BITWIDTH_12);
    return fail_channel ? ESP_FAIL : ESP_OK;
}
esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t unit, adc_channel_t channel, int *raw)
{
    assert(unit);
    *raw = channel == 3 ? raw_x : channel == 4 ? raw_y : raw_light;
    return fail_read ? ESP_ERR_TIMEOUT : ESP_OK;
}
int main(void)
{
    levels[0] = levels[6] = 1;
    levels[13] = levels[14] = levels[41] = 1;
    const pet_button_input_t *input = pet_bsp_confirm_button();
    assert(input->init() == ESP_OK && !input->pressed() && units == 0);
#if CONFIG_PET_EXT_JOYSTICK
    assert(input->navigation_init() == ESP_ERR_NO_MEM);
    levels[0] = 0;
    assert(input->pressed()); /* Failed extension never removes the base button. */
    fail_create = false; fail_channel = true;
    assert(input->navigation_init() == ESP_FAIL);
    fail_channel = false;
    assert(input->navigation_init() == ESP_OK);
    pet_navigation_sample_t sample;
    assert(input->navigation_read(&sample) == ESP_OK && sample.valid && !sample.pressed);
#if CONFIG_PET_JOYSTICK_INVERT_X && CONFIG_PET_JOYSTICK_INVERT_Y
    assert(sample.x == 2047 && sample.y == 2047 && units == 1);
#else
    assert(sample.x == 2048 && sample.y == 2048 && units == 1);
#endif
    levels[6] = 0; raw_x = 0; raw_y = 4095;
    assert(input->navigation_read(&sample) == ESP_OK && sample.pressed);
#if CONFIG_PET_JOYSTICK_INVERT_X && CONFIG_PET_JOYSTICK_INVERT_Y
    assert(sample.x == 4095 && sample.y == 0);
#else
    assert(sample.x == 0 && sample.y == 4095);
#endif
    assert(input->navigation_read(NULL) == ESP_ERR_INVALID_ARG);
    fail_read = true;
    assert(input->navigation_read(&sample) == ESP_ERR_TIMEOUT && !sample.valid);
    fail_read = false; raw_x = -1;
    assert(input->navigation_read(&sample) == ESP_ERR_INVALID_ARG && !sample.valid);
    raw_x = 2048;
    assert(input->navigation_read(&sample) == ESP_OK && sample.valid);
#else
    assert(!input->navigation_init && !input->navigation_read);
#endif
#if CONFIG_PET_EXT_LIGHT_SENSOR
    uint16_t light;
#if !CONFIG_PET_EXT_JOYSTICK
    assert(pet_bsp_ambient_read(&light) == ESP_ERR_NO_MEM);
    fail_create = false; fail_channel = true;
    assert(pet_bsp_ambient_read(&light) == ESP_FAIL);
    fail_channel = false;
#endif
    assert(pet_bsp_ambient_read(&light) == ESP_OK && units == 1);
#if CONFIG_PET_LIGHT_SENSOR_INVERT
    assert(light == 2895);
#else
    assert(light == 1200);
#endif
    fail_read = true;
    assert(pet_bsp_ambient_read(&light) == ESP_ERR_TIMEOUT);
    fail_read = false; raw_light = 5000;
    assert(pet_bsp_ambient_read(&light) == ESP_ERR_INVALID_ARG);
    assert(pet_bsp_ambient_read(NULL) == ESP_ERR_INVALID_ARG);
#endif
#if CONFIG_PET_INPUT_TRIPLE
    assert(input->keys_init && input->keys_read);
    assert(input->keys_init() == ESP_OK);
    bool keys[PET_KEY_COUNT];
    input->keys_read(keys);
    assert(!keys[PET_KEY_CONFIRM] && !keys[PET_KEY_NEXT] && !keys[PET_KEY_VOICE]); /* 高电平=释放 */
    levels[41] = 0;
    input->keys_read(keys);
    assert(!keys[PET_KEY_CONFIRM] && !keys[PET_KEY_NEXT] && keys[PET_KEY_VOICE]); /* active-low: 0=按下 */
    input->keys_read(NULL); /* 空指针安全。 */
#else
    assert(!input->keys_init && !input->keys_read);
#endif
#if !CONFIG_PET_EXT_JOYSTICK && !CONFIG_PET_EXT_LIGHT_SENSOR
    assert(units == 0);
#endif
    puts("BSP input: real GPIO/ADC adapter, failure fallback and shared ADC1 passed");
}
