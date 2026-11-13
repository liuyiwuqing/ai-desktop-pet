#include "svc_audio/api.h"

#include <math.h>

#include "esp_timer.h"
#include "pet/hal/audio_pcm.h"

static bool cancelled(const pet_audio_duplex_config_t *config)
{
    return config->cancelled != NULL && config->cancelled(config->ctx);
}

static void erase_pcm(int16_t *pcm, size_t samples)
{
    /* 诊断不向调用方暴露录音；volatile 保留退出时对临时 PCM 的清除。 */
    volatile int16_t *target = pcm;
    for (size_t i = 0; i < samples; ++i) target[i] = 0;
}

static void tone(int16_t *pcm, size_t offset, size_t total, uint8_t volume)
{
    for (size_t i = 0; i < PET_AUDIO_PLAYBACK_SAMPLES; ++i) {
        const size_t index = offset + i;
        const size_t edge = index < total - 1 - index ? index : total - 1 - index;
        const int32_t ramp = edge < 240 ? (int32_t)edge : 240; /* 首尾 10 ms 淡入淡出。 */
        const int32_t sample = (int32_t)lround(10000.0 * sin((double)(index % 60) * (6.283185307179586 / 60.0)));
        pcm[i] = (int16_t)(sample * volume * ramp / 24000);
    }
}

esp_err_t pet_audio_duplex_run(const pet_audio_device_t *device,
                              const pet_audio_duplex_config_t *config,
                              pet_audio_duplex_report_t *report)
{
    if (report == NULL) return ESP_ERR_INVALID_ARG;
    *report = (pet_audio_duplex_report_t){.phase = PET_AUDIO_CHECK_INIT};
    if (device == NULL || config == NULL || device->init == NULL || device->deinit == NULL ||
        device->capture_read == NULL || device->capture_stop == NULL ||
        device->playback_write == NULL || device->playback_drain == NULL || device->playback_stop == NULL ||
        config->duration_ms == 0 || config->duration_ms > 1000 ||
        config->duration_ms % PET_AUDIO_FRAME_MS != 0 || config->volume_pct > 25)
        return ESP_ERR_INVALID_ARG;
    if (device->duplex_start == NULL) return ESP_ERR_NOT_SUPPORTED;

    int16_t input[PET_AUDIO_CAPTURE_SAMPLES] = {0}, output[PET_AUDIO_PLAYBACK_SAMPLES] = {0};
    const int64_t started = esp_timer_get_time();
    const int64_t deadline = started + (int64_t)(config->duration_ms + 250) * 1000;
    const size_t frames = config->duration_ms / PET_AUDIO_FRAME_MS;
    const size_t total = frames * PET_AUDIO_PLAYBACK_SAMPLES;
    bool owned = false;
    esp_err_t err = PET_AUDIO_ERR_CANCELLED;
    if (cancelled(config)) goto cleanup;
    owned = true;
    err = device->init();
    if (err != ESP_OK) goto cleanup;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    if (esp_timer_get_time() > deadline) { err = ESP_ERR_TIMEOUT; goto cleanup; }
    report->phase = PET_AUDIO_CHECK_DUPLEX;
    err = device->duplex_start(PET_AUDIO_PLAYBACK_RATE);
    if (err != ESP_OK) goto cleanup;
    report->duplex_started = true;
    int64_t last_capture = esp_timer_get_time();
    for (size_t frame = 0; frame < frames; ++frame) {
        if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
        if (esp_timer_get_time() > deadline) { err = ESP_ERR_TIMEOUT; goto cleanup; }
        tone(output, frame * PET_AUDIO_PLAYBACK_SAMPLES, total, config->volume_pct);
        size_t written = 0;
        err = device->playback_write(output, PET_AUDIO_PLAYBACK_SAMPLES, &written, 100);
        if (written > PET_AUDIO_PLAYBACK_SAMPLES) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        report->submitted_samples += written;
        if (err != ESP_OK) goto cleanup;
        if (written != PET_AUDIO_PLAYBACK_SAMPLES) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        size_t received = 0;
        err = device->capture_read(input, PET_AUDIO_CAPTURE_SAMPLES, &received, 100);
        if (received > PET_AUDIO_CAPTURE_SAMPLES) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        report->recorded_samples += received;
        if (err != ESP_OK) goto cleanup;
        if (received != PET_AUDIO_CAPTURE_SAMPLES) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        const int64_t observed = esp_timer_get_time();
        const uint64_t gap = (uint64_t)(observed - last_capture);
        if (gap > report->max_capture_gap_us) report->max_capture_gap_us = gap;
        last_capture = observed;
        pet_audio_level_t level;
        pet_audio_pcm_measure(input, received, &level);
        if (level.peak > report->capture_peak) report->capture_peak = level.peak;
        erase_pcm(input, PET_AUDIO_CAPTURE_SAMPLES);
        if (observed > deadline) { err = ESP_ERR_TIMEOUT; goto cleanup; }
    }
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    /* drain 最多阻塞 250 ms，先停 RX，避免把尾部等待期间的溢出当成完整采集。 */
    err = device->capture_stop();
    if (err != ESP_OK) goto cleanup;
    report->phase = PET_AUDIO_CHECK_DRAIN;
    err = device->playback_drain(250);
    if (err != ESP_OK) goto cleanup;
    report->playback_drained = true;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    err = device->playback_stop();

cleanup:
    if (owned) report->cleanup_error = device->deinit();
    erase_pcm(input, PET_AUDIO_CAPTURE_SAMPLES);
    erase_pcm(output, PET_AUDIO_PLAYBACK_SAMPLES);
    report->elapsed_us = (uint64_t)(esp_timer_get_time() - started);
    if (err == ESP_OK && report->cleanup_error != ESP_OK) err = report->cleanup_error;
    if (err == ESP_OK) report->phase = PET_AUDIO_CHECK_DONE;
    return err;
}
