#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "svc_audio/api.h"

static bool initialized, capturing, playing, drained;
static size_t capture_offset, playback_offset;
static int16_t heard[PET_AUDIO_BUFFER_SAMPLES];
typedef enum { NONE, INIT, CAPTURE_START, CAPTURE_READ, CAPTURE_STOP,
               PLAYBACK_START, PLAYBACK_WRITE, DRAIN, PLAYBACK_STOP,
               SHORT_READ, SHORT_WRITE, OVER_READ, OVER_WRITE } fault_t;
static fault_t fault;
static bool fail_cleanup;
static int cancel_at; /* 1: before init, 2: after capture frame, 3: after TX frame, 4: during drain */

static esp_err_t init(void)
{
    assert(!initialized);
    initialized = true;
    drained = false;
    capture_offset = playback_offset = 0;
    memset(heard, 0, sizeof(heard));
    return fault == INIT ? ESP_FAIL : ESP_OK;
}
static esp_err_t deinit(void)
{
    if (fail_cleanup) return ESP_ERR_NO_MEM;
    initialized = capturing = playing = false;
    return ESP_OK;
}
static esp_err_t capture_start(void)
{
    assert(initialized && !playing);
    capturing = true;
    return fault == CAPTURE_START ? ESP_FAIL : ESP_OK;
}
static esp_err_t capture_read(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout)
{
    assert(capturing && !playing && timeout > 0);
    for (size_t i = 0; i < capacity; ++i) pcm[i] = (int16_t)((int)(capture_offset++ % 2000) - 1000);
    *samples = fault == SHORT_READ ? capacity - 1 : fault == OVER_READ ? capacity + 1 : capacity;
    return fault == CAPTURE_READ ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t capture_stop(void)
{
    if (fault == CAPTURE_STOP) return ESP_FAIL;
    capturing = false;
    return ESP_OK;
}
static esp_err_t playback_start(uint32_t rate)
{
    assert(initialized && !capturing && rate == 16000);
    playing = true;
    return fault == PLAYBACK_START ? ESP_FAIL : ESP_OK;
}
static esp_err_t playback_write(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout)
{
    assert(playing && !capturing && timeout > 0);
    assert(playback_offset + samples <= PET_AUDIO_BUFFER_SAMPLES);
    memcpy(heard + playback_offset, pcm, samples * sizeof(*pcm));
    playback_offset += samples;
    *written = fault == SHORT_WRITE ? samples - 1 : fault == OVER_WRITE ? samples + 1 : samples;
    return fault == PLAYBACK_WRITE ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t playback_drain(uint32_t timeout)
{
    assert(playing && timeout > 0);
    if (fault == DRAIN) return ESP_ERR_TIMEOUT;
    drained = true;
    return ESP_OK;
}
static esp_err_t playback_stop(void)
{
    assert(drained);
    if (fault == PLAYBACK_STOP) return ESP_FAIL;
    playing = false;
    return ESP_OK;
}
static bool cancelled(void *ctx)
{
    assert(ctx == &cancel_at);
    return cancel_at == 1 || (cancel_at == 2 && capturing && capture_offset >= 320) ||
           (cancel_at == 3 && playing && playback_offset >= 320) || (cancel_at == 4 && drained);
}
static const pet_audio_device_t device = {
    .id = "host-audio-boundary", .init = init, .deinit = deinit,
    .capture_start = capture_start, .capture_read = capture_read, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = playback_write,
    .playback_drain = playback_drain, .playback_stop = playback_stop,
};

int main(void)
{
    int16_t storage[PET_AUDIO_BUFFER_SAMPLES];
    pet_audio_loopback_report_t report;
    pet_audio_loopback_config_t config = {
        .duration_ms = 1000, .volume_pct = 50,
        .storage = storage, .storage_samples = PET_AUDIO_BUFFER_SAMPLES,
    };
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_OK);
    assert(report.recorded_samples == 16000 && report.submitted_samples == 16000);
    assert(report.phase == PET_AUDIO_CHECK_DONE && report.playback_drained);
    assert(report.capture_level.peak == 1000 && report.cleanup_error == ESP_OK);
    assert(!initialized && !capturing && !playing && drained);
    for (size_t i = 0; i < 16000; ++i) {
        const int16_t expected = (int16_t)((int)(i % 2000) - 1000);
        assert(heard[i] == expected / 2);
        assert(storage[i] == 0);
    }

    /* 每一阶段失败都释放麦克风/播放资源，且相同配置可再次完整执行。 */
    for (fault_t scenario = INIT; scenario <= OVER_WRITE; ++scenario) {
        fault = scenario;
        assert(pet_audio_loopback_run(&device, &config, &report) != ESP_OK);
        assert(report.phase != PET_AUDIO_CHECK_DONE && report.cleanup_error == ESP_OK);
        assert(!initialized && !capturing && !playing);
        if (scenario <= CAPTURE_STOP || scenario == SHORT_READ || scenario == OVER_READ)
            assert(report.submitted_samples == 0);
        if (scenario == DRAIN) assert(!report.playback_drained);
        for (size_t i = 0; i < 16000; ++i) assert(storage[i] == 0);
        fault = NONE;
        assert(pet_audio_loopback_run(&device, &config, &report) == ESP_OK);
        assert(heard[0] == -500 && heard[15999] == 499);
    }

    config.cancelled = cancelled;
    config.ctx = &cancel_at;
    for (cancel_at = 1; cancel_at <= 4; ++cancel_at) {
        assert(pet_audio_loopback_run(&device, &config, &report) == PET_AUDIO_ERR_CANCELLED);
        assert(!initialized && !capturing && !playing && report.playback_drained == (cancel_at == 4));
        if (cancel_at < 3) assert(report.submitted_samples == 0);
        for (size_t i = 0; i < 16000; ++i) assert(storage[i] == 0);
    }
    cancel_at = 0;
    fail_cleanup = true;
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_ERR_NO_MEM);
    assert(report.phase != PET_AUDIO_CHECK_DONE && report.cleanup_error == ESP_ERR_NO_MEM);
    fail_cleanup = false;
    assert(deinit() == ESP_OK);
    fault = CAPTURE_READ;
    fail_cleanup = true;
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_ERR_TIMEOUT);
    assert(report.cleanup_error == ESP_ERR_NO_MEM);
    fail_cleanup = false;
    assert(deinit() == ESP_OK);
    fault = NONE;

    config.volume_pct = 101;
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_ERR_INVALID_ARG);
    config.volume_pct = 50;
    config.duration_ms = 21;
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_ERR_INVALID_ARG);
    config.duration_ms = 1000;
    config.storage_samples = 15999;
    assert(pet_audio_loopback_run(&device, &config, &report) == ESP_ERR_INVALID_SIZE);
    assert(!initialized);
    puts("audio loopback: half-duplex, errors, cancellation, retry, drain and erasure passed");
}
