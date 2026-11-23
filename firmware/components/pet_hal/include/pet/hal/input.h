#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "pet/hal/keys.h"
#include "pet/hal/navigation.h"

typedef struct {
    esp_err_t (*init)(void);
    bool (*pressed)(void);
    /* Optional extension; recovery and physical confirmations use BOOT only. */
    esp_err_t (*navigation_init)(void);
    esp_err_t (*navigation_read)(pet_navigation_sample_t *sample);
    /* Optional triple-key layer; NULL when CONFIG_PET_INPUT_TRIPLE is off. */
    esp_err_t (*keys_init)(void);
    void (*keys_read)(bool pressed[PET_KEY_COUNT]);
} pet_button_input_t;

const pet_button_input_t *pet_bsp_confirm_button(void);
