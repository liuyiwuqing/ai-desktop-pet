#include <assert.h>
#include <stdio.h>

#include "svc_audio/api.h"

int main(void)
{
    const pet_audio_device_t *fake = pet_fake_audio();
    int16_t storage[320];
    pet_audio_loopback_report_t report;
    const pet_audio_loopback_config_t config = {
        .duration_ms = 20, .volume_pct = 0, .storage = storage, .storage_samples = 320,
    };
    assert(fake->capture_start() == ESP_ERR_INVALID_STATE);
    assert(pet_audio_loopback_run(fake, &config, &report) == ESP_OK);
    assert(report.recorded_samples == 320 && report.submitted_samples == 320);
    assert(report.capture_level.peak == 1000 && report.capture_level.rms == 1000);
    assert(report.playback_drained && report.phase == PET_AUDIO_CHECK_DONE);
    assert(pet_audio_loopback_run(fake, &config, &report) == ESP_OK);
    assert(fake->capture_start() == ESP_ERR_INVALID_STATE);
    for (size_t i = 0; i < 320; ++i) assert(storage[i] == 0);
    puts("fake audio: explicit synthetic loopback and repeat passed (no hardware)");
}
