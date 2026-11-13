#include "sdkconfig.h"

#if CONFIG_PET_MODULE_AUDIO_ENABLED

#include "pet/hal/audio.h"

#if CONFIG_PET_AUDIO_FAKE

const pet_audio_device_t *pet_bsp_audio(void)
{
    return pet_fake_audio();
}

#else

#include <stdbool.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_generated.h"
#include "pet/hal/audio_pcm.h"

#define CAPTURE_DMA_BUFFERS 4u
#define PLAYBACK_DMA_BUFFERS 3u
#define AUDIO_IO_TIMEOUT_MAX_MS 1000u

static i2s_chan_handle_t s_rx, s_tx;
static bool s_ready, s_capturing, s_playing;
static int32_t s_raw[PET_AUDIO_CAPTURE_SAMPLES];
static int16_t s_stereo[PET_AUDIO_PLAYBACK_SAMPLES * 2];
static DRAM_ATTR portMUX_TYPE s_eof_lock = portMUX_INITIALIZER_UNLOCKED;
static DRAM_ATTR uint32_t s_tx_eofs;

static bool IRAM_ATTR on_tx_sent(i2s_chan_handle_t channel, i2s_event_data_t *event, void *ctx)
{
    (void)channel;
    (void)event;
    (void)ctx;
    portENTER_CRITICAL_ISR(&s_eof_lock);
    ++s_tx_eofs;
    portEXIT_CRITICAL_ISR(&s_eof_lock);
    return false;
}

static uint32_t tx_eofs(void)
{
    portENTER_CRITICAL(&s_eof_lock);
    const uint32_t count = s_tx_eofs;
    portEXIT_CRITICAL(&s_eof_lock);
    return count;
}

static esp_err_t capture_stop(void)
{
    if (!s_capturing) return ESP_OK;
    const esp_err_t err = i2s_channel_disable(s_rx);
    if (err == ESP_OK) s_capturing = false;
    memset(s_raw, 0, sizeof(s_raw));
    return err;
}

static esp_err_t playback_stop(void)
{
    if (!s_playing) return ESP_OK;
    const esp_err_t err = i2s_channel_disable(s_tx);
    if (err == ESP_OK) s_playing = false;
    memset(s_stereo, 0, sizeof(s_stereo));
    return err;
}

static void first_error(esp_err_t *result, esp_err_t err)
{
    if (*result == ESP_OK && err != ESP_OK) *result = err;
}

static esp_err_t audio_deinit(void)
{
    esp_err_t err = ESP_OK;
    first_error(&err, capture_stop());
    first_error(&err, playback_stop());
    if (s_rx != NULL && !s_capturing) {
        const esp_err_t deleted = i2s_del_channel(s_rx);
        first_error(&err, deleted);
        if (deleted == ESP_OK) s_rx = NULL;
    }
    if (s_tx != NULL && !s_playing) {
        const esp_err_t deleted = i2s_del_channel(s_tx);
        first_error(&err, deleted);
        if (deleted == ESP_OK) s_tx = NULL;
    }
    s_ready = false;
    memset(s_raw, 0, sizeof(s_raw));
    memset(s_stereo, 0, sizeof(s_stereo));
    return err;
}

static esp_err_t audio_init(void)
{
    if (s_ready) return ESP_OK;
    if (s_rx != NULL || s_tx != NULL) return ESP_ERR_INVALID_STATE;
#if CONFIG_PET_MODULE_CLIMATE_ENABLED && !CONFIG_PET_CLIMATE_FAKE_SENSOR
    /* DHT11 可在 menuconfig 覆盖默认脚；不能让覆盖值悄悄抢占音频总线。 */
    const int audio_pins[] = {PET_BOARD_MIC_BCLK, PET_BOARD_MIC_WS, PET_BOARD_MIC_DIN,
                             PET_BOARD_SPEAKER_BCLK, PET_BOARD_SPEAKER_WS, PET_BOARD_SPEAKER_DOUT};
    for (size_t i = 0; i < sizeof(audio_pins) / sizeof(audio_pins[0]); ++i)
        if (CONFIG_PET_CLIMATE_DHT11_GPIO == audio_pins[i]) return ESP_ERR_INVALID_ARG;
#endif

    i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(PET_BOARD_MIC_I2S, I2S_ROLE_MASTER);
    channel.dma_desc_num = CAPTURE_DMA_BUFFERS;
    channel.dma_frame_num = PET_AUDIO_CAPTURE_SAMPLES;
    esp_err_t err = i2s_new_channel(&channel, NULL, &s_rx);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t rx = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PET_AUDIO_CAPTURE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = PET_BOARD_MIC_BCLK,
                     .ws = PET_BOARD_MIC_WS, .dout = I2S_GPIO_UNUSED, .din = PET_BOARD_MIC_DIN},
    };
    rx.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    err = i2s_channel_init_std_mode(s_rx, &rx);
    if (err != ESP_OK) goto fail;

    channel = (i2s_chan_config_t)I2S_CHANNEL_DEFAULT_CONFIG(PET_BOARD_SPEAKER_I2S, I2S_ROLE_MASTER);
    channel.dma_desc_num = PLAYBACK_DMA_BUFFERS;
    channel.dma_frame_num = PET_AUDIO_PLAYBACK_SAMPLES;
    /* 欠载发送零，不让 DMA 环反复播放旧语音。 */
    channel.auto_clear_after_cb = true;
    err = i2s_new_channel(&channel, &s_tx, NULL);
    if (err != ESP_OK) goto fail;
    i2s_std_config_t tx = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(PET_AUDIO_PLAYBACK_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {.mclk = I2S_GPIO_UNUSED, .bclk = PET_BOARD_SPEAKER_BCLK,
                     .ws = PET_BOARD_SPEAKER_WS, .dout = PET_BOARD_SPEAKER_DOUT, .din = I2S_GPIO_UNUSED},
    };
    err = i2s_channel_init_std_mode(s_tx, &tx);
    if (err != ESP_OK) goto fail;
    const i2s_event_callbacks_t callbacks = {.on_sent = on_tx_sent};
    err = i2s_channel_register_event_callback(s_tx, &callbacks, NULL);
    if (err != ESP_OK) goto fail;
    s_ready = true;
    return ESP_OK;

fail:
    audio_deinit(); /* 不掩盖初始化原始错误；若清理失败，句柄保留供 deinit 重试。 */
    return err;
}

static esp_err_t capture_start(void)
{
    if (!s_ready || s_capturing || s_playing) return ESP_ERR_INVALID_STATE;
    const esp_err_t err = i2s_channel_enable(s_rx);
    if (err == ESP_OK) s_capturing = true;
    return err;
}

static esp_err_t capture_read(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout_ms)
{
    if (samples != NULL) *samples = 0;
    if (pcm == NULL || samples == NULL || capacity == 0 || capacity > PET_AUDIO_CAPTURE_SAMPLES ||
        timeout_ms == 0 || timeout_ms > AUDIO_IO_TIMEOUT_MAX_MS) return ESP_ERR_INVALID_ARG;
    if (!s_capturing) return ESP_ERR_INVALID_STATE;
    size_t bytes = 0;
    const esp_err_t err = i2s_channel_read(s_rx, s_raw, capacity * sizeof(*s_raw), &bytes, timeout_ms);
    if (bytes > capacity * sizeof(*s_raw) || bytes % sizeof(*s_raw) != 0) return ESP_ERR_INVALID_SIZE;
    *samples = bytes / sizeof(*s_raw);
    for (size_t i = 0; i < *samples; ++i) pcm[i] = pet_audio_pcm_from_i2s(s_raw[i]);
    memset(s_raw, 0, sizeof(s_raw));
    return err;
}

static esp_err_t prepare_playback(uint32_t sample_rate)
{
    const i2s_std_clk_config_t clock = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate);
    esp_err_t err = i2s_channel_reconfig_std_clock(s_tx, &clock);
    if (err != ESP_OK) return err;
    /* 每次启动先覆盖整个 DMA 环，停止/重启不能带出上一段录音或半帧尾部。 */
    memset(s_stereo, 0, sizeof(s_stereo));
    for (unsigned i = 0; i < PLAYBACK_DMA_BUFFERS; ++i) {
        size_t loaded = 0;
        err = i2s_channel_preload_data(s_tx, s_stereo, sizeof(s_stereo), &loaded);
        if (err != ESP_OK) return err;
        if (loaded != sizeof(s_stereo)) return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}

static esp_err_t playback_start(uint32_t sample_rate)
{
    if (sample_rate != PET_AUDIO_CAPTURE_RATE && sample_rate != PET_AUDIO_PLAYBACK_RATE)
        return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_capturing || s_playing) return ESP_ERR_INVALID_STATE;
    esp_err_t err = prepare_playback(sample_rate);
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(s_tx);
    if (err == ESP_OK) s_playing = true;
    return err;
}

static esp_err_t duplex_start(uint32_t sample_rate)
{
    if (sample_rate != PET_AUDIO_CAPTURE_RATE && sample_rate != PET_AUDIO_PLAYBACK_RATE)
        return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_capturing || s_playing) return ESP_ERR_INVALID_STATE;
    if (PET_BOARD_MIC_I2S == PET_BOARD_SPEAKER_I2S) return ESP_ERR_NOT_SUPPORTED;
    /* 先准备静音 TX 环，准备失败时不开麦。两个独立控制器仍各用自己的时钟；
     * 这里没有把写入 DMA 的时间冒充播放参考时间。 */
    esp_err_t err = prepare_playback(sample_rate);
    if (err != ESP_OK) return err;
    err = capture_start();
    if (err != ESP_OK) return err;
    err = i2s_channel_enable(s_tx);
    if (err == ESP_OK) s_playing = true;
    else (void)capture_stop(); /* 保留原错误和未清理的句柄，供调用方 deinit 重试。 */
    return err;
}

static esp_err_t playback_write(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout_ms)
{
    if (written != NULL) *written = 0;
    if (pcm == NULL || written == NULL || samples == 0 || samples > PET_AUDIO_PLAYBACK_SAMPLES ||
        timeout_ms == 0 || timeout_ms > AUDIO_IO_TIMEOUT_MAX_MS) return ESP_ERR_INVALID_ARG;
    if (!s_playing) return ESP_ERR_INVALID_STATE;
    /* 两槽同源，MAX98357A 无论选左/右/混合都保持相同单声道内容。 */
    for (size_t i = 0; i < samples; ++i) s_stereo[2 * i] = s_stereo[2 * i + 1] = pcm[i];
    size_t bytes = 0;
    const esp_err_t err = i2s_channel_write(s_tx, s_stereo, samples * 4, &bytes, timeout_ms);
    memset(s_stereo, 0, sizeof(s_stereo));
    if (bytes > samples * 4 || bytes % 4 != 0) return ESP_ERR_INVALID_SIZE;
    *written = bytes / 4;
    return err;
}

static esp_err_t playback_drain(uint32_t timeout_ms)
{
    if (!s_playing) return ESP_ERR_INVALID_STATE;
    if (timeout_ms == 0 || timeout_ms > AUDIO_IO_TIMEOUT_MAX_MS) return ESP_ERR_INVALID_ARG;
    const uint32_t start_eof = tx_eofs();
    const int64_t start_us = esp_timer_get_time();
    /*
     * write 返回只表示拷入 DMA。再等一个完整环 + 一帧的 EOF，覆盖最后一帧
     * 及 I2S FIFO 尾部；auto_clear 已把经过的描述符归零。不能用固定 sleep 判完成。
     */
    while ((uint32_t)(tx_eofs() - start_eof) < PLAYBACK_DMA_BUFFERS + 1) {
        if (esp_timer_get_time() - start_us >= (int64_t)timeout_ms * 1000) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    return ESP_OK;
}

static const pet_audio_device_t s_audio = {
    .id = "dodo-i2s-inmp441-max98357a", .init = audio_init, .deinit = audio_deinit,
    .capture_start = capture_start, .capture_read = capture_read, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = playback_write,
    .playback_drain = playback_drain, .playback_stop = playback_stop,
    .duplex_start = duplex_start,
};

const pet_audio_device_t *pet_bsp_audio(void)
{
    return &s_audio;
}

#endif
#endif
