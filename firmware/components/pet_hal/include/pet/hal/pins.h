#pragma once
#include <stdbool.h>
/* 排除板上已接外设、Flash/PSRAM、USB、输入专用与启动绑带引脚。 */
bool pet_bsp_gpio_available(int gpio);
