#pragma once

#include "esp_err.h"

#include "pet/climate.h"
#include "pet/hal/climate_sensor.h"

/* 温湿度模块对外入口。sensor 由组合根注入，模块不直接依赖 BSP。 */
esp_err_t pet_climate_init(const pet_climate_sensor_t *sensor);
esp_err_t pet_climate_start(void);
esp_err_t pet_climate_stop(void);
esp_err_t pet_climate_get_snapshot(pet_climate_snapshot_t *out);
esp_err_t pet_climate_set_calibration(float temperature_offset_c, float humidity_offset_pct);
