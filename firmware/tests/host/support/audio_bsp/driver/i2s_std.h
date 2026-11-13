#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* 只替代硬件/IDF 边界，BSP 使用真实源码；真实类型兼容性由 ESP-IDF 构建验证。 */
typedef struct host_i2s_channel *i2s_chan_handle_t;
typedef struct { size_t size; void *dma_buf; } i2s_event_data_t;
typedef bool (*i2s_isr_callback_t)(i2s_chan_handle_t, i2s_event_data_t *, void *);
typedef struct { i2s_isr_callback_t on_sent; } i2s_event_callbacks_t;
enum { I2S_ROLE_MASTER, I2S_SLOT_MODE_MONO, I2S_SLOT_MODE_STEREO,
       I2S_STD_SLOT_LEFT, I2S_STD_SLOT_BOTH, I2S_GPIO_UNUSED = -1 };
enum { I2S_DATA_BIT_WIDTH_16BIT = 16, I2S_DATA_BIT_WIDTH_32BIT = 32 };
typedef struct {
    int id, role;
    unsigned dma_desc_num, dma_frame_num;
    bool auto_clear_after_cb;
} i2s_chan_config_t;
typedef struct { uint32_t sample_rate_hz; } i2s_std_clk_config_t;
typedef struct { int data_bit_width, slot_mode, slot_mask; } i2s_std_slot_config_t;
typedef struct { int mclk, bclk, ws, dout, din; } i2s_std_gpio_config_t;
typedef struct {
    i2s_std_clk_config_t clk_cfg;
    i2s_std_slot_config_t slot_cfg;
    i2s_std_gpio_config_t gpio_cfg;
} i2s_std_config_t;

#define I2S_CHANNEL_DEFAULT_CONFIG(port, mode) \
    {.id = (port), .role = (mode), .dma_desc_num = 6, .dma_frame_num = 240}
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {.sample_rate_hz = (rate)}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits, mode) \
    {.data_bit_width = (bits), .slot_mode = (mode), .slot_mask = I2S_STD_SLOT_BOTH}

esp_err_t i2s_new_channel(const i2s_chan_config_t *, i2s_chan_handle_t *, i2s_chan_handle_t *);
esp_err_t i2s_del_channel(i2s_chan_handle_t);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t, const i2s_std_config_t *);
esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t, const i2s_event_callbacks_t *, void *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_channel_read(i2s_chan_handle_t, void *, size_t, size_t *, uint32_t);
esp_err_t i2s_channel_write(i2s_chan_handle_t, const void *, size_t, size_t *, uint32_t);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t, const void *, size_t, size_t *);
esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t, const i2s_std_clk_config_t *);
