#pragma once
#include "esp_err.h"
/* ADC1 belongs to the UI task; optional analog inputs share one unit. */
esp_err_t pet_bsp_analog_read(int gpio, int *raw);
