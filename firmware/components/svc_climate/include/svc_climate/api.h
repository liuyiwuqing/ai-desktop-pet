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
esp_err_t pet_climate_get_chart(pet_climate_chart_t *out);
/* 只通知采样拥有者；仍满足 DHT 最短两秒间隔，不在 UI 调用传感器。 */
esp_err_t pet_climate_retry(void);
