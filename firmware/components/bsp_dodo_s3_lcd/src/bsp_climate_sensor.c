#include "sdkconfig.h"

#if CONFIG_PET_MODULE_CLIMATE_ENABLED

#include "pet/hal/climate_sensor.h"

#if CONFIG_PET_CLIMATE_FAKE_SENSOR

const pet_climate_sensor_t *pet_bsp_climate_sensor(void)
{
    return pet_fake_climate_sensor();
}

#else

#include <string.h>

#include "driver/gpio.h"
#include "driver/rmt_common.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "pet/hal/climate_decode.h"

#define DHT11_GPIO ((gpio_num_t)CONFIG_PET_CLIMATE_DHT11_GPIO)
#define DHT11_RMT_SYMBOLS 48
#define DHT11_START_LOW_US 18000
#define DHT11_IDLE_END_NS 2000000

static const char *TAG = "bsp_climate";

static rmt_channel_handle_t s_channel;
static SemaphoreHandle_t s_done;
static rmt_symbol_word_t s_symbols[DHT11_RMT_SYMBOLS];
static volatile size_t s_symbol_count;
static bool s_ready;

static bool on_recv_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *ctx)
{
    (void)channel;
    (void)ctx;

    BaseType_t woken = pdFALSE;
    s_symbol_count = edata->num_symbols;
    xSemaphoreGiveFromISR(s_done, &woken);
    return woken == pdTRUE;
}

/* RMT 边沿流 → 40 bit 帧；跳过 18 ms 起始低电平与 80/80 µs 响应头。 */
static bool frame_from_symbols(const rmt_symbol_word_t *symbols, size_t count, uint8_t frame[5])
{
    uint8_t bits[40] = {0};
    uint8_t bit_count = 0;
    bool response_seen = false;
    bool skip_response_high = false;

    for (size_t i = 0; i < count; i++) {
        const uint32_t durations[2] = {symbols[i].duration0, symbols[i].duration1};
        const uint32_t levels[2] = {symbols[i].level0, symbols[i].level1};

        for (int edge = 0; edge < 2; edge++) {
            const uint32_t duration = durations[edge];
            const uint32_t level = levels[edge];
            if (duration == 0) {
                continue;
            }

            if (!response_seen) {
                if (level == 0 && duration >= 40 && duration <= 200) {
                    response_seen = true;
                    skip_response_high = true;
                }
                continue;
            }

            if (skip_response_high) {
                if (level == 1) {
                    skip_response_high = false;
                }
                continue;
            }

            if (level == 1 && bit_count < 40) {
                bits[bit_count++] = (duration > 50) ? 1 : 0;
            }
        }
    }

    if (bit_count < 40) {
        return false;
    }

    memset(frame, 0, 5);
    for (uint8_t i = 0; i < 40; i++) {
        frame[i / 8] = (uint8_t)((frame[i / 8] << 1) | bits[i]);
    }
    return true;
}

static esp_err_t dht11_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    s_done = xSemaphoreCreateBinary();
    if (s_done == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const rmt_rx_channel_config_t rx_config = {
        .gpio_num = DHT11_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,
        .mem_block_symbols = DHT11_RMT_SYMBOLS,
        .intr_priority = 0,
        .flags = {
            .invert_in = false,
            .with_dma = false,
            .allow_pd = false,
        },
    };

    esp_err_t err = rmt_new_rx_channel(&rx_config, &s_channel);
    if (err != ESP_OK) {
        return err;
    }

    const rmt_rx_event_callbacks_t callbacks = {
        .on_recv_done = on_recv_done,
    };
    err = rmt_rx_register_event_callbacks(s_channel, &callbacks, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = rmt_enable(s_channel);
    if (err != ESP_OK) {
        return err;
    }

    const gpio_config_t io_config = {
        .pin_bit_mask = 1ULL << CONFIG_PET_CLIMATE_DHT11_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&io_config);
    if (err != ESP_OK) {
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "DHT11 ready on GPIO%d", CONFIG_PET_CLIMATE_DHT11_GPIO);
    return ESP_OK;
}

static esp_err_t dht11_read(pet_climate_reading_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const rmt_receive_config_t rx_config = {
        .signal_range_min_ns = 1000,
        .signal_range_max_ns = DHT11_IDLE_END_NS,
        .flags = {
            .en_partial_rx = false,
        },
    };

    s_symbol_count = 0;
    xSemaphoreTake(s_done, 0);
    esp_err_t err = rmt_receive(s_channel, s_symbols, sizeof(s_symbols), &rx_config);
    if (err != ESP_OK) {
        return err;
    }

    /* 主机拉低 ≥18 ms 触发采样，随后释放为输入由上拉恢复高电平。 */
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_GPIO, 0);
    esp_rom_delay_us(DHT11_START_LOW_US);
    gpio_set_level(DHT11_GPIO, 1);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    if (xSemaphoreTake(s_done, pdMS_TO_TICKS(50)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t frame[5];
    if (!frame_from_symbols(s_symbols, s_symbol_count, frame)) {
        return ESP_ERR_INVALID_CRC;
    }

    float temperature = 0.0f;
    float humidity = 0.0f;
    const pet_climate_error_t decode_error = pet_climate_decode_dht11(frame, &temperature, &humidity);
    if (decode_error != PET_CLIMATE_ERR_NONE) {
        return decode_error == PET_CLIMATE_ERR_CHECKSUM ? ESP_ERR_INVALID_CRC : ESP_ERR_INVALID_ARG;
    }

    out->temperature_c = temperature;
    out->humidity_pct = humidity;
    out->timestamp_ms = 0;
    out->error_code = PET_CLIMATE_ERR_NONE;
    out->quality = 100;
    out->valid = true;
    return ESP_OK;
}

static const pet_climate_sensor_t s_dht11_sensor = {
    .id = "dht11",
    .init = dht11_init,
    .read = dht11_read,
};

const pet_climate_sensor_t *pet_bsp_climate_sensor(void)
{
    return &s_dht11_sensor;
}

#endif /* CONFIG_PET_CLIMATE_FAKE_SENSOR */

#endif /* CONFIG_PET_MODULE_CLIMATE_ENABLED */
