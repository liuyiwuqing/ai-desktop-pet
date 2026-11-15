#include "pet/hal/display.h"
#include "sdkconfig.h"

#if CONFIG_PET_MODULE_UI_ENABLED
#include <string.h>
#include "board_generated.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define STRIP_BYTES (PET_BOARD_LCD_WIDTH * 20 * sizeof(uint16_t))

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_done;
static void *s_dma;
static bool s_pending;
static bool s_initialized;

static bool color_done(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *event,
                       void *context)
{
    (void)io;
    (void)event;
    (void)context;
    BaseType_t wake = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &wake);
    return wake == pdTRUE;
}

static esp_err_t backlight(uint8_t percent)
{
    if (!s_initialized)
        return ESP_ERR_INVALID_STATE;
    if (percent > 100)
        percent = 100;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, percent * 1023U / 100U);
    return err == ESP_OK ? ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0) : err;
}

static esp_err_t init(void)
{
    if (s_initialized)
        return ESP_ERR_INVALID_STATE;
    bool bus_ready = false;
    s_done = xSemaphoreCreateBinary();
    s_dma = heap_caps_malloc(STRIP_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    esp_err_t err = ESP_ERR_NO_MEM;
    if (s_done == NULL || s_dma == NULL)
        goto fail;
    const spi_bus_config_t bus = {
        .mosi_io_num = PET_BOARD_LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = PET_BOARD_LCD_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = STRIP_BYTES,
    };
    err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK)
        goto fail;
    bus_ready = true;
    const esp_lcd_panel_io_spi_config_t io = {
        .cs_gpio_num = PET_BOARD_LCD_CS,
        .dc_gpio_num = PET_BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = 40000000,
        .trans_queue_depth = 1,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .on_color_trans_done = color_done,
    };
    err = esp_lcd_new_panel_io_spi(SPI3_HOST, &io, &s_io);
    if (err != ESP_OK)
        goto fail;
    const esp_lcd_panel_dev_config_t panel = {
        .reset_gpio_num = PET_BOARD_LCD_RESET,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    err = esp_lcd_new_panel_st7789(s_io, &panel, &s_panel);
    if (err != ESP_OK)
        goto fail;
    if ((err = esp_lcd_panel_reset(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_init(s_panel)) != ESP_OK ||
        (err = esp_lcd_panel_invert_color(s_panel, PET_BOARD_LCD_INVERT_COLOR)) != ESP_OK ||
        (err = esp_lcd_panel_swap_xy(s_panel, false)) != ESP_OK ||
        (err = esp_lcd_panel_mirror(s_panel, false, false)) != ESP_OK ||
        (err = esp_lcd_panel_disp_on_off(s_panel, true)) != ESP_OK)
        goto fail;
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = LEDC_TIMER_0,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    if ((err = ledc_timer_config(&timer)) != ESP_OK)
        goto fail;
    const ledc_channel_config_t channel = {
        .gpio_num = PET_BOARD_LCD_BACKLIGHT,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    if ((err = ledc_channel_config(&channel)) != ESP_OK)
        goto fail;
    s_initialized = true;
    return ESP_OK;
fail:
    if (s_panel != NULL) {
        esp_lcd_panel_del(s_panel);
        s_panel = NULL;
    }
    if (s_io != NULL) {
        esp_lcd_panel_io_del(s_io);
        s_io = NULL;
    }
    if (bus_ready)
        spi_bus_free(SPI3_HOST);
    if (s_done != NULL) {
        vSemaphoreDelete(s_done);
        s_done = NULL;
    }
    heap_caps_free(s_dma);
    s_dma = NULL;
    return err;
}

static esp_err_t write_pixels(int x1, int y1, int x2, int y2, const void *pixels,
                              uint32_t timeout_ms)
{
    if (!s_initialized)
        return ESP_ERR_INVALID_STATE;
    if (pixels == NULL || x1 < 0 || y1 < 0 || x2 <= x1 || y2 <= y1 || x2 > PET_BOARD_LCD_WIDTH ||
        y2 > PET_BOARD_LCD_HEIGHT)
        return ESP_ERR_INVALID_ARG;
    const size_t length = (size_t)(x2 - x1) * (y2 - y1) * sizeof(uint16_t);
    if (length > STRIP_BYTES)
        return ESP_ERR_INVALID_SIZE;
    /* 超时后的 DMA 仍可能运行。保留自有缓冲区，收到完成信号前绝不覆盖。 */
    if (s_pending) {
        if (xSemaphoreTake(s_done, 0) != pdTRUE)
            return ESP_ERR_TIMEOUT;
        s_pending = false;
    }
    xSemaphoreTake(s_done, 0);
    memcpy(s_dma, pixels, length);
    s_pending = true;
    const esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x1, y1, x2, y2, s_dma);
    if (err != ESP_OK) {
        s_pending = false;
        return err;
    }
    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    s_pending = false;
    return ESP_OK;
}

const pet_display_t *pet_bsp_display(void)
{
    static const pet_display_t display = {
        .width = PET_BOARD_LCD_WIDTH,
        .height = PET_BOARD_LCD_HEIGHT,
        .init = init,
        .write = write_pixels,
        .backlight = backlight,
#if CONFIG_PET_EXT_LIGHT_SENSOR
        .ambient_read = pet_bsp_ambient_read,
#endif
    };
    return &display;
}
#endif
