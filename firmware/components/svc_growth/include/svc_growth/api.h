#pragma once

#include "esp_err.h"

/* 成长模块对外入口。S2 仅验证组件边界与编译开关，不含业务逻辑。 */
esp_err_t pet_growth_init(void);
