#include "svc_audio/api.h"

#include <string.h>

#include "pet/hal/audio_buffer.h"
#include "pet/hal/audio_pcm.h"

static bool cancelled(const pet_audio_loopback_config_t *config)
{
    return config->cancelled != NULL && config->cancelled(config->ctx);
}

esp_err_t pet_audio_loopback_run(const pet_audio_device_t *device,
                                 const pet_audio_loopback_config_t *config,
                                 pet_audio_loopback_report_t *report)
{
    if (report == NULL) return ESP_ERR_INVALID_ARG;
    *report = (pet_audio_loopback_report_t){.phase = PET_AUDIO_CHECK_INIT};
    if (device == NULL || config == NULL || device->init == NULL || device->deinit == NULL ||
        device->capture_start == NULL || device->capture_read == NULL || device->capture_stop == NULL ||
        device->playback_start == NULL || device->playback_write == NULL ||
        device->playback_drain == NULL || device->playback_stop == NULL ||
        config->duration_ms == 0 || config->duration_ms > 1000 ||
        config->duration_ms % PET_AUDIO_FRAME_MS != 0 || config->volume_pct > 100)
        return ESP_ERR_INVALID_ARG;

    const size_t samples = config->duration_ms / PET_AUDIO_FRAME_MS * PET_AUDIO_CAPTURE_SAMPLES;
    if (config->storage == NULL || config->storage_samples < samples) return ESP_ERR_INVALID_SIZE;
    pet_audio_buffer_t buffer;
    if (!pet_audio_buffer_init(&buffer, config->storage, samples)) return ESP_ERR_INVALID_SIZE;
    int16_t frame[PET_AUDIO_CAPTURE_SAMPLES] = {0};
    bool owned = false;
    esp_err_t err = PET_AUDIO_ERR_CANCELLED;
    if (cancelled(config)) goto cleanup;
    owned = true;
    err = device->init();
    if (err != ESP_OK) goto cleanup;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }

    report->phase = PET_AUDIO_CHECK_CAPTURE;
    err = device->capture_start();
    if (err != ESP_OK) goto cleanup;
    for (size_t offset = 0; offset < samples; offset += PET_AUDIO_CAPTURE_SAMPLES) {
        if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
        size_t received = 0;
        err = device->capture_read(frame, PET_AUDIO_CAPTURE_SAMPLES, &received, 100);
        if (received > PET_AUDIO_CAPTURE_SAMPLES) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        report->recorded_samples += received;
        if (err != ESP_OK) goto cleanup;
        if (received != PET_AUDIO_CAPTURE_SAMPLES || !pet_audio_buffer_push(&buffer, frame, received)) {
            err = ESP_ERR_INVALID_SIZE;
            goto cleanup;
        }
    }
    err = device->capture_stop();
    if (err != ESP_OK) goto cleanup;
    pet_audio_pcm_measure(config->storage, samples, &report->capture_level);

    report->phase = PET_AUDIO_CHECK_PLAYBACK;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    /* 诊断录音保持原采样率；按 24 kHz 播会造成音高和时长变化。 */
    err = device->playback_start(PET_AUDIO_CAPTURE_RATE);
    if (err != ESP_OK) goto cleanup;
    while (buffer.count > 0) {
        if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
        const size_t count = pet_audio_buffer_read(&buffer, frame, PET_AUDIO_CAPTURE_SAMPLES);
        pet_audio_pcm_volume(frame, frame, count, config->volume_pct);
        size_t written = 0;
        err = device->playback_write(frame, count, &written, 100);
        if (written > count) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
        report->submitted_samples += written;
        if (err != ESP_OK) goto cleanup;
        if (written != count) { err = ESP_ERR_INVALID_SIZE; goto cleanup; }
    }
    report->phase = PET_AUDIO_CHECK_DRAIN;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    err = device->playback_drain(250);
    if (err != ESP_OK) goto cleanup;
    report->playback_drained = true;
    if (cancelled(config)) { err = PET_AUDIO_ERR_CANCELLED; goto cleanup; }
    err = device->playback_stop();

cleanup:
    if (owned) report->cleanup_error = device->deinit();
    pet_audio_buffer_clear(&buffer);
    memset(frame, 0, sizeof(frame));
    if (err == ESP_OK && report->cleanup_error != ESP_OK) err = report->cleanup_error;
    if (err == ESP_OK) report->phase = PET_AUDIO_CHECK_DONE;
    return err;
}
