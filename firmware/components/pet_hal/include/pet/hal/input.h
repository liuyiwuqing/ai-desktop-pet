#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef struct {
    esp_err_t (*init)(void);
    bool (*pressed)(void);
} pet_button_input_t;

const pet_button_input_t *pet_bsp_confirm_button(void);
