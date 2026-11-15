#pragma once

#include <stdint.h>
#include "esp_err.h"

/* RGB565 大端像素；write 在返回前消费调用方缓冲区，超时也不得继续引用它。 */
typedef struct {
    uint16_t width;
    uint16_t height;
    esp_err_t (*init)(void);
    esp_err_t (*write)(int x1, int y1, int x2, int y2, const void *pixels, uint32_t timeout_ms);
    esp_err_t (*backlight)(uint8_t percent);
    /* Optional 12-bit relative light input; normal UI only, never recovery. */
    esp_err_t (*ambient_read)(uint16_t *raw);
} pet_display_t;

const pet_display_t *pet_bsp_display(void);
esp_err_t pet_bsp_ambient_read(uint16_t *raw);
