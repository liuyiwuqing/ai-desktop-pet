#include "pet/hal/input.h"
#include "sdkconfig.h"

#if CONFIG_PET_MODULE_UI_ENABLED
#include "board_generated.h"
#include "driver/gpio.h"

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
    static const pet_button_input_t input = {.init = init, .pressed = pressed};
    return &input;
}
#endif
