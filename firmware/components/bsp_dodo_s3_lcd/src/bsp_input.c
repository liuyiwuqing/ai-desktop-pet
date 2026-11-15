#include "pet/hal/input.h"
#include "sdkconfig.h"

#if CONFIG_PET_MODULE_UI_ENABLED
#include "board_generated.h"
#include "driver/gpio.h"
#if CONFIG_PET_EXT_JOYSTICK
#include "bsp_analog.h"

static esp_err_t navigation_read(pet_navigation_sample_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    *out = (pet_navigation_sample_t){0};
    int x, y;
    esp_err_t error = pet_bsp_analog_read(PET_BOARD_JOYSTICK_X, &x);
    if (error == ESP_OK) error = pet_bsp_analog_read(PET_BOARD_JOYSTICK_Y, &y);
    if (error != ESP_OK) return error;
    if (x < 0 || x > 4095 || y < 0 || y > 4095) return ESP_ERR_INVALID_ARG;
    out->x = x;
    out->y = y;
#if CONFIG_PET_JOYSTICK_INVERT_X
    out->x = 4095 - x;
#endif
#if CONFIG_PET_JOYSTICK_INVERT_Y
    out->y = 4095 - y;
#endif
    out->pressed = gpio_get_level(PET_BOARD_JOYSTICK_SELECT) == 0;
    out->valid = true;
    return ESP_OK;
}

static esp_err_t navigation_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PET_BOARD_JOYSTICK_SELECT,
        .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    esp_err_t error = gpio_config(&config);
    pet_navigation_sample_t sample;
    return error == ESP_OK ? navigation_read(&sample) : error;
}
#endif

static esp_err_t init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << PET_BOARD_BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    return gpio_config(&config);
}

static bool pressed(void) { return gpio_get_level(PET_BOARD_BOOT_GPIO) == 0; }

const pet_button_input_t *pet_bsp_confirm_button(void)
{
    static const pet_button_input_t input = {.init = init, .pressed = pressed,
#if CONFIG_PET_EXT_JOYSTICK
        .navigation_init = navigation_init, .navigation_read = navigation_read,
#endif
    };
    return &input;
}
#endif
