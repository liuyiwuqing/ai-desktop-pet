#pragma once

#include "esp_err.h"

#include "pet/climate.h"

/* 温湿度传感器 HAL 接口：业务只依赖该结构体，板级实现由 BSP 提供。 */

typedef struct {
    const char *id;
    esp_err_t (*init)(void);
    esp_err_t (*read)(pet_climate_reading_t *out);
} pet_climate_sensor_t;

/* 板级默认传感器；由 BSP 组件实现（可能返回 fake）。 */
const pet_climate_sensor_t *pet_bsp_climate_sensor(void);

/* 确定性假传感器；仅在 CONFIG_PET_CLIMATE_FAKE_SENSOR 时编入。 */
const pet_climate_sensor_t *pet_fake_climate_sensor(void);
