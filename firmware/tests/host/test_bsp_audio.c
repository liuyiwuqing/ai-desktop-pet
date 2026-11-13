#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "driver/i2s_std.h"
#include "pet/hal/audio.h"

struct host_i2s_channel {
    bool allocated, ready, enabled, tx;
    i2s_chan_config_t channel;
    i2s_std_config_t config;
    i2s_isr_callback_t callback;
    size_t preloaded;
    int64_t next_eof;
};
static struct host_i2s_channel channels[2];
typedef enum { NONE, NEW_RX, RX_CONFIG, NEW_TX, TX_CONFIG, CALLBACK, ENABLE_RX, DISABLE_RX,
               RECLOCK, PRELOAD, ENABLE_TX, READ, WRITE, DISABLE_TX, DELETE_RX, DELETE_TX } fault_t;
static fault_t fault;
static int64_t now_us;
static bool suppress_dma;
static unsigned sent_callbacks;
static int16_t transmitted[PET_AUDIO_PLAYBACK_SAMPLES * 2];
static size_t override_bytes = SIZE_MAX;

static bool fail(fault_t at)
{
    if (fault != at) return false;
    fault = NONE;
    return true;
}

esp_err_t i2s_new_channel(const i2s_chan_config_t *config, i2s_chan_handle_t *tx, i2s_chan_handle_t *rx)
{
    assert((tx != NULL) != (rx != NULL));
    assert(config->id == (tx ? 1 : 0));
    if (fail(tx ? NEW_TX : NEW_RX)) return ESP_ERR_NO_MEM;
    struct host_i2s_channel *channel = &channels[config->id];
    assert(!channel->allocated);
    *channel = (struct host_i2s_channel){.allocated = true, .tx = tx != NULL, .channel = *config};
    if (tx) *tx = channel; else *rx = channel;
    return ESP_OK;
}
esp_err_t i2s_del_channel(i2s_chan_handle_t channel)
{
    assert(channel->allocated && !channel->enabled);
    if (fail(channel->tx ? DELETE_TX : DELETE_RX)) return ESP_FAIL;
    memset(channel, 0, sizeof(*channel));
    return ESP_OK;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t channel, const i2s_std_config_t *config)
{
    assert(channel->allocated && !channel->enabled);
    if (fail(channel->tx ? TX_CONFIG : RX_CONFIG)) return ESP_FAIL;
    channel->ready = true;
    channel->config = *config;
    return ESP_OK;
}
esp_err_t i2s_channel_register_event_callback(i2s_chan_handle_t channel,
                                             const i2s_event_callbacks_t *callbacks, void *ctx)
{
    assert(channel->ready && channel->tx && ctx == NULL);
    if (fail(CALLBACK)) return ESP_FAIL;
    channel->callback = callbacks->on_sent;
    return ESP_OK;
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t channel)
{
    assert(channel->ready && !channel->enabled);
    if (fail(channel->tx ? ENABLE_TX : ENABLE_RX)) return ESP_FAIL;
    channel->enabled = true;
    channel->next_eof = now_us + channel->channel.dma_frame_num * 1000000LL / channel->config.clk_cfg.sample_rate_hz;
    return ESP_OK;
}
esp_err_t i2s_channel_disable(i2s_chan_handle_t channel)
{
    assert(channel->enabled);
    if (fail(channel->tx ? DISABLE_TX : DISABLE_RX)) return ESP_FAIL;
    channel->enabled = false;
    channel->preloaded = 0;
    return ESP_OK;
}
esp_err_t i2s_channel_read(i2s_chan_handle_t channel, void *dest, size_t bytes, size_t *read, uint32_t timeout)
{
    assert(channel->enabled && !channel->tx && timeout > 0);
    static const int32_t raw[] = {0x7fffff00, INT32_MIN, 0x12345600, -65536};
    int32_t *out = dest;
    for (size_t i = 0; i < bytes / 4; ++i) out[i] = raw[i % 4];
    *read = override_bytes == SIZE_MAX ? bytes : override_bytes;
    return fail(READ) ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t channel, const void *data, size_t bytes, size_t *written, uint32_t timeout)
{
    assert(channel->enabled && channel->tx && timeout > 0 && bytes <= sizeof(transmitted));
    memcpy(transmitted, data, bytes);
    *written = override_bytes == SIZE_MAX ? bytes : override_bytes;
    return fail(WRITE) ? ESP_ERR_TIMEOUT : ESP_OK;
}
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t channel, const void *data, size_t bytes, size_t *loaded)
{
    assert(channel->ready && channel->tx && !channel->enabled);
    if (fail(PRELOAD)) return ESP_FAIL;
    const unsigned char *zero = data;
    for (size_t i = 0; i < bytes; ++i) assert(zero[i] == 0);
    channel->preloaded += bytes;
    assert(channel->preloaded <= 3 * 480 * 4);
    *loaded = bytes;
    return ESP_OK;
}
esp_err_t i2s_channel_reconfig_std_clock(i2s_chan_handle_t channel, const i2s_std_clk_config_t *clock)
{
    assert(channel->ready && !channel->enabled && channel->tx);
    if (fail(RECLOCK)) return ESP_FAIL;
    channel->config.clk_cfg = *clock;
    return ESP_OK;
}
int64_t esp_timer_get_time(void) { return now_us; }
void vTaskDelay(uint32_t ticks)
{
    now_us += ticks * 1000;
    struct host_i2s_channel *channel = &channels[1];
    if (channel->enabled && channel->callback && !suppress_dma && now_us >= channel->next_eof) {
        channel->next_eof += channel->channel.dma_frame_num * 1000000LL / channel->config.clk_cfg.sample_rate_hz;
        assert(!channel->callback(channel, NULL, NULL));
        ++sent_callbacks;
    }
}

int main(void)
{
    const pet_audio_device_t *audio = pet_bsp_audio();
#if CONFIG_PET_CLIMATE_DHT11_GPIO == 17
    assert(audio->init() == ESP_ERR_INVALID_ARG);
    assert(!channels[0].allocated && !channels[1].allocated);
    puts("BSP audio: DHT override conflict rejected before I2S allocation");
    return 0;
#endif
    assert(audio->capture_start() == ESP_ERR_INVALID_STATE);
    for (fault_t scenario = NEW_RX; scenario <= CALLBACK; ++scenario) {
        fault = scenario;
        assert(audio->init() != ESP_OK);
        assert(!channels[0].allocated && !channels[1].allocated);
        assert(audio->deinit() == ESP_OK);
    }
    assert(audio->init() == ESP_OK && audio->init() == ESP_OK);
    assert(!channels[0].enabled && !channels[1].enabled);
    assert(channels[0].config.slot_cfg.data_bit_width == 32 &&
           channels[0].config.slot_cfg.slot_mask == I2S_STD_SLOT_LEFT);
    assert(channels[0].config.gpio_cfg.bclk == 16 && channels[0].config.gpio_cfg.ws == 17 &&
           channels[0].config.gpio_cfg.din == 18);
    assert(channels[0].channel.dma_desc_num == 4 && channels[0].channel.dma_frame_num == 320);
    assert(channels[1].channel.dma_desc_num == 3 && channels[1].channel.dma_frame_num == 480);
    assert(channels[1].channel.auto_clear_after_cb);
    assert(channels[1].config.gpio_cfg.bclk == 21 && channels[1].config.gpio_cfg.ws == 38 &&
           channels[1].config.gpio_cfg.dout == 39);

    fault = ENABLE_RX;
    assert(audio->capture_start() == ESP_FAIL && !channels[0].enabled);
    assert(audio->capture_start() == ESP_OK);
    assert(audio->playback_start(24000) == ESP_ERR_INVALID_STATE);
    int16_t samples[4]; size_t count = 99;
    assert(audio->capture_read(samples, 4, &count, 100) == ESP_OK && count == 4);
    assert(samples[0] == 32767 && samples[1] == -32768 && samples[2] == 0x1234 && samples[3] == -1);
    override_bytes = 8; fault = READ;
    assert(audio->capture_read(samples, 4, &count, 100) == ESP_ERR_TIMEOUT && count == 2);
    override_bytes = 3;
    assert(audio->capture_read(samples, 4, &count, 100) == ESP_ERR_INVALID_SIZE && count == 0);
    override_bytes = SIZE_MAX;
    assert(audio->capture_read(samples, 321, &count, 100) == ESP_ERR_INVALID_ARG);
    fault = DISABLE_RX;
    assert(audio->deinit() == ESP_FAIL && channels[0].enabled);
    assert(audio->deinit() == ESP_OK && !channels[0].allocated && !channels[1].allocated);

    for (uint32_t rate = 16000; rate <= 24000; rate += 8000) {
        assert(audio->init() == ESP_OK);
        assert(audio->playback_start(22050) == ESP_ERR_INVALID_ARG);
        assert(audio->playback_start(rate) == ESP_OK);
        assert(channels[1].preloaded == 3 * 480 * 4);
        assert(audio->capture_start() == ESP_ERR_INVALID_STATE);
        const int16_t pcm[] = {1000, -2000, 32767, -32768};
        assert(audio->playback_write(pcm, 4, &count, 100) == ESP_OK && count == 4);
        for (size_t i = 0; i < 4; ++i) assert(transmitted[2 * i] == pcm[i] && transmitted[2 * i + 1] == pcm[i]);
        override_bytes = 8; fault = WRITE;
        assert(audio->playback_write(pcm, 4, &count, 100) == ESP_ERR_TIMEOUT && count == 2);
        override_bytes = 3;
        assert(audio->playback_write(pcm, 4, &count, 100) == ESP_ERR_INVALID_SIZE && count == 0);
        override_bytes = SIZE_MAX;
        suppress_dma = true;
        assert(audio->playback_drain(10) == ESP_ERR_TIMEOUT && channels[1].enabled);
        suppress_dma = false;
        const unsigned sent_before = sent_callbacks;
        const int64_t before = now_us;
        assert(audio->playback_drain(250) == ESP_OK);
        assert(sent_callbacks - sent_before == 4 && now_us - before >= (rate == 16000 ? 110000 : 70000));
        assert(audio->playback_stop() == ESP_OK && !channels[1].enabled);
        assert(audio->playback_stop() == ESP_OK);
        /* 含短帧的播放停止后，下一次启动仍必须预填整环静音。 */
        assert(audio->playback_start(rate) == ESP_OK);
        assert(channels[1].preloaded == 3 * 480 * 4);
        fault = DISABLE_TX;
        assert(audio->deinit() == ESP_FAIL && channels[1].enabled);
        assert(audio->deinit() == ESP_OK);
    }
    for (fault_t scenario = RECLOCK; scenario <= ENABLE_TX; ++scenario) {
        assert(audio->init() == ESP_OK);
        fault = scenario;
        assert(audio->playback_start(24000) != ESP_OK);
        assert(audio->deinit() == ESP_OK);
    }
    for (fault_t scenario = DELETE_RX; scenario <= DELETE_TX; ++scenario) {
        assert(audio->init() == ESP_OK);
        fault = scenario;
        assert(audio->deinit() == ESP_FAIL);
        assert(audio->init() == ESP_ERR_INVALID_STATE);
        assert(audio->deinit() == ESP_OK);
    }
    puts("BSP audio: I2S format, half-duplex, DMA drain, timeout, retry and cleanup passed");
}
