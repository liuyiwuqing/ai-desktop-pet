#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "svc_audio/api.h"

typedef enum { NONE, INIT, START, READ, SHORT_READ, OVER_READ, WRITE, SHORT_WRITE, OVER_WRITE,
               STOP_CAPTURE, DRAIN, STOP_PLAYBACK } fault_t;
static fault_t fault;
static bool initialized, capturing, playing, fail_cleanup, drained, stall;
static unsigned init_calls, captured_frames, written_frames, cancel_at;
static uint32_t submitted;
static int64_t now_us;
static int16_t heard[24000];

int64_t esp_timer_get_time(void) { return now_us; }
static esp_err_t init(void)
{
    assert(!initialized); initialized = true; drained = false;
    captured_frames = written_frames = submitted = 0;
    memset(heard, 0, sizeof(heard)); ++init_calls;
    return fault == INIT ? ESP_FAIL : ESP_OK;
}
static esp_err_t deinit(void)
{
    if (fail_cleanup) return ESP_ERR_NO_MEM;
    initialized = capturing = playing = false;
    return ESP_OK;
}
static esp_err_t start(uint32_t rate)
{
    assert(initialized && !capturing && !playing && rate == 24000);
    capturing = playing = true;
    return fault == START ? ESP_FAIL : ESP_OK;
}
static esp_err_t read_pcm(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout)
{
    assert(capturing && playing && capacity == 320 && timeout == 100);
    for (size_t i = 0; i < capacity; ++i) pcm[i] = i % 2 ? INT16_MIN : INT16_MAX;
    *samples = fault == SHORT_READ ? 319 : fault == OVER_READ ? 321 : 320;
    ++captured_frames; now_us += stall ? 200000 : 20000;
    return fault == READ ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t write_pcm(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout)
{
    assert(capturing && playing && samples == 480 && timeout == 100);
    assert(submitted + samples <= sizeof(heard) / sizeof(heard[0]));
    for (size_t i = 0; i < samples; ++i) assert(pcm[i] >= -2500 && pcm[i] <= 2500);
    memcpy(heard + submitted, pcm, samples * sizeof(*pcm)); submitted += samples;
    *written = fault == SHORT_WRITE ? 479 : fault == OVER_WRITE ? 481 : 480;
    ++written_frames; now_us += 1000;
    return fault == WRITE ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t stop_capture(void)
{
    assert(capturing && playing);
    if (fault == STOP_CAPTURE) return ESP_FAIL;
    capturing = false; return ESP_OK;
}
static esp_err_t drain(uint32_t timeout)
{
    assert(!capturing && playing && timeout == 250);
    now_us += 80000;
    if (fault == DRAIN) return ESP_ERR_TIMEOUT;
    drained = true; return ESP_OK;
}
static esp_err_t stop_playback(void)
{
    assert(!capturing && playing && drained);
    if (fault == STOP_PLAYBACK) return ESP_FAIL;
    playing = false; return ESP_OK;
}
static bool cancelled(void *ctx)
{
    assert(ctx == &cancel_at);
    return cancel_at == 1 || (cancel_at == 2 && initialized && captured_frames > 0) ||
           (cancel_at == 3 && initialized && drained);
}
static const pet_audio_device_t device = {
    .id = "host-parallel-audio-boundary", .init = init, .deinit = deinit, .duplex_start = start,
    .capture_read = read_pcm, .capture_stop = stop_capture,
    .playback_write = write_pcm, .playback_drain = drain, .playback_stop = stop_playback,
};

int main(void)
{
    pet_audio_duplex_config_t config = {.duration_ms = 1000, .volume_pct = 25};
    pet_audio_duplex_report_t report;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_OK);
    assert(report.phase == PET_AUDIO_CHECK_DONE && report.duplex_started && report.playback_drained);
    assert(report.recorded_samples == 16000 && report.submitted_samples == 24000);
    assert(captured_frames == 50 && written_frames == 50 && report.capture_peak == 32768);
    assert(report.max_capture_gap_us == 21000 && report.elapsed_us == 1130000);
    assert(heard[0] == 0 && heard[23999] == 0);
    bool nonzero = false;
    for (size_t i = 0; i < 24000; ++i) nonzero |= heard[i] != 0;
    assert(nonzero && !initialized && !capturing && !playing);
    for (fault_t scenario = INIT; scenario <= STOP_PLAYBACK; ++scenario) {
        fault = scenario;
        assert(pet_audio_duplex_run(&device, &config, &report) != ESP_OK);
        assert(report.phase != PET_AUDIO_CHECK_DONE && report.cleanup_error == ESP_OK);
        assert(!initialized && !capturing && !playing);
        if (scenario <= START) assert(report.recorded_samples == 0 && report.submitted_samples == 0);
        if (scenario <= DRAIN) assert(!report.playback_drained);
        fault = NONE;
        assert(pet_audio_duplex_run(&device, &config, &report) == ESP_OK);
    }
    config.cancelled = cancelled; config.ctx = &cancel_at;
    for (cancel_at = 1; cancel_at <= 3; ++cancel_at) {
        assert(pet_audio_duplex_run(&device, &config, &report) == PET_AUDIO_ERR_CANCELLED);
        assert(!initialized && !capturing && !playing);
        assert(report.playback_drained == (cancel_at == 3));
    }
    cancel_at = 0;
    fail_cleanup = true;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_ERR_NO_MEM);
    assert(report.cleanup_error == ESP_ERR_NO_MEM && report.phase != PET_AUDIO_CHECK_DONE);
    fail_cleanup = false; assert(deinit() == ESP_OK);
    fault = READ; fail_cleanup = true;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_ERR_TIMEOUT);
    assert(report.cleanup_error == ESP_ERR_NO_MEM);
    fail_cleanup = false; assert(deinit() == ESP_OK); fault = NONE;
    stall = true;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_ERR_TIMEOUT);
    assert(!initialized && !capturing && !playing && !report.playback_drained);
    stall = false;
    unsigned before = init_calls;
    pet_audio_device_t unsupported = device; unsupported.duplex_start = NULL;
    assert(pet_audio_duplex_run(&unsupported, &config, &report) == ESP_ERR_NOT_SUPPORTED);
    config.volume_pct = 26;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_ERR_INVALID_ARG);
    config.volume_pct = 25; config.duration_ms = 21;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_ERR_INVALID_ARG);
    assert(init_calls == before);
    config.duration_ms = 20; config.volume_pct = 0;
    assert(pet_audio_duplex_run(&device, &config, &report) == ESP_OK);
    assert(report.recorded_samples == 320 && report.submitted_samples == 480);
    for (size_t i = 0; i < 480; ++i) assert(heard[i] == 0);
    puts("duplex diagnostic: bounded parallel RX/TX, synthesized-only output, errors, cancellation and cleanup passed");
}
