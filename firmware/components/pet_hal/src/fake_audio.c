#include "pet/hal/audio.h"

#include <stdbool.h>

/* CI/桌面使用的确定性方波。drain 为逻辑完成，不能作为 I2S/扬声器验收证据。 */
static bool s_ready, s_capturing, s_playing;
static uint32_t s_index;

static esp_err_t fake_init(void) { s_ready = true; return ESP_OK; }
static esp_err_t fake_deinit(void)
{
    s_ready = s_capturing = s_playing = false;
    s_index = 0;
    return ESP_OK;
}
static esp_err_t capture_start(void)
{
    if (!s_ready || s_capturing || s_playing) return ESP_ERR_INVALID_STATE;
    s_capturing = true;
    s_index = 0;
    return ESP_OK;
}
static esp_err_t capture_read(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout_ms)
{
    if (samples != NULL) *samples = 0;
    if (pcm == NULL || samples == NULL || capacity == 0 || capacity > PET_AUDIO_CAPTURE_SAMPLES ||
        timeout_ms == 0 || timeout_ms > 1000) return ESP_ERR_INVALID_ARG;
    if (!s_capturing) return ESP_ERR_INVALID_STATE;
    for (size_t i = 0; i < capacity; ++i) pcm[i] = (s_index++ / 20) % 2 ? 1000 : -1000;
    *samples = capacity;
    return ESP_OK;
}
static esp_err_t capture_stop(void) { s_capturing = false; return ESP_OK; }
static esp_err_t playback_start(uint32_t rate)
{
    if (rate != PET_AUDIO_CAPTURE_RATE && rate != PET_AUDIO_PLAYBACK_RATE) return ESP_ERR_INVALID_ARG;
    if (!s_ready || s_playing || s_capturing) return ESP_ERR_INVALID_STATE;
    s_playing = true;
    return ESP_OK;
}
static esp_err_t playback_write(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout_ms)
{
    if (written != NULL) *written = 0;
    if (pcm == NULL || written == NULL || samples == 0 || samples > PET_AUDIO_PLAYBACK_SAMPLES ||
        timeout_ms == 0 || timeout_ms > 1000) return ESP_ERR_INVALID_ARG;
    if (!s_playing) return ESP_ERR_INVALID_STATE;
    *written = samples;
    return ESP_OK;
}
static esp_err_t playback_drain(uint32_t timeout_ms)
{
    if (timeout_ms == 0 || timeout_ms > 1000) return ESP_ERR_INVALID_ARG;
    return s_playing ? ESP_OK : ESP_ERR_INVALID_STATE;
}
static esp_err_t playback_stop(void) { s_playing = false; return ESP_OK; }

static const pet_audio_device_t s_fake = {
    .id = "fake-audio-400hz", .init = fake_init, .deinit = fake_deinit,
    .capture_start = capture_start, .capture_read = capture_read, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = playback_write,
    .playback_drain = playback_drain, .playback_stop = playback_stop,
};

const pet_audio_device_t *pet_fake_audio(void)
{
    return &s_fake;
}
