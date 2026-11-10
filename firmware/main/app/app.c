#include "app.h"

#include <inttypes.h>

#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "pet/event_bus.h"

#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "svc_growth/api.h"
#endif

static const char *TAG = "app";

static void on_boot(const pet_event_t *event)
{
    ESP_LOGI(TAG, "boot event: uptime=%" PRIu32 " ms", event->timestamp_ms);
}

esp_err_t app_start(void)
{
    ESP_LOGI(TAG, "reset_reason=%d, free_heap=%u, free_psram=%u",
             esp_reset_reason(),
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_ERROR_CHECK(pet_event_bus_init());
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_BOOT, on_boot));

#if CONFIG_PET_MODULE_GROWTH_ENABLED
    ESP_ERROR_CHECK(pet_growth_init());
#endif

    const pet_event_t boot = {
        .id = PET_EVT_BOOT,
        .timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL),
    };
    ESP_ERROR_CHECK(pet_event_bus_publish(&boot));

    ESP_LOGI(TAG, "skeleton started, idf=%s", esp_get_idf_version());
    return ESP_OK;
}
