#include "svc_audio/api.h"

#include <inttypes.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_check";
static bool s_started;

static void diagnostic_task(void *ctx)
{
    int16_t *storage = ctx;
    const pet_audio_device_t *device = pet_bsp_audio();
    const pet_audio_loopback_config_t config = {
        .duration_ms = 1000, .volume_pct = 25,
        .storage = storage, .storage_samples = PET_AUDIO_BUFFER_SAMPLES,
    };
    ESP_LOGW(TAG, "%s: explicit boot diagnostic; recording 1 second, then playback at 25%%", device->id);
    pet_audio_loopback_report_t report;
    const esp_err_t err = pet_audio_loopback_run(device, &config, &report);
    ESP_LOGI(TAG, "result=%s phase=%d recorded=%" PRIu32 " submitted=%" PRIu32
             " drained=%d rms=%u peak=%u level=%u cleanup=%s",
             esp_err_to_name(err), report.phase, report.recorded_samples, report.submitted_samples,
             report.playback_drained, report.capture_level.rms, report.capture_level.peak,
             report.capture_level.level_pct, esp_err_to_name(report.cleanup_error));
    heap_caps_free(storage);
    vTaskDelete(NULL);
}

esp_err_t pet_audio_diagnostic_start(void)
{
    /* 仅组合根在启动时调用一次，不占用 BOOT 或成长菜单的现有交互。 */
    if (s_started) return ESP_ERR_INVALID_STATE;
    int16_t *storage = heap_caps_calloc(PET_AUDIO_BUFFER_SAMPLES, sizeof(int16_t),
                                      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (storage == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(diagnostic_task, "pet_audio_check", 6144, storage, 3, NULL) != pdPASS) {
        heap_caps_free(storage);
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
    return ESP_OK;
}
