#pragma once

#include <stdint.h>

#include "pet/climate.h"

/* DHT11/DHT22 40 bit 帧解码：纯函数，主机可测。 */
pet_climate_error_t pet_climate_decode_dht11(const uint8_t frame[5],
                                             float *temperature_c,
                                             float *humidity_pct);
