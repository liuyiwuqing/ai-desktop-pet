#include "app.h"

#include <inttypes.h>
#include <string.h>

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

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#include "pet/hal/climate_sensor.h"
#include "pet/presentation/climate_card.h"
#include "svc_climate/api.h"
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/device_control.h"
#endif

#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo.h"
#endif

static const char *TAG = "app";

static void on_boot(const pet_event_t *event)
{
    ESP_LOGI(TAG, "boot event: uptime=%" PRIu32 " ms, seq=%" PRIu32,
             event->timestamp_ms, event->sequence);
}

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
static const char *alert_text(pet_climate_alert_t alert)
{
    switch (alert) {
    case PET_CLIMATE_ALERT_COLD:
        return "cold";
    case PET_CLIMATE_ALERT_HOT:
        return "hot";
    case PET_CLIMATE_ALERT_DRY:
        return "dry";
    case PET_CLIMATE_ALERT_HUMID:
        return "humid";
    case PET_CLIMATE_ALERT_CONDENSATION:
        return "condensation";
    default:
        return "none";
    }
}

/* 首版以串口日志充当 UI 出口；LVGL 卡片后续复用同一快照。 */
static void on_climate_reading(const pet_event_t *event)
{
    pet_climate_reading_t reading;
    if (event->payload_len < sizeof(reading)) {
        return;
    }
    memcpy(&reading, event->payload, sizeof(reading));

    pet_climate_snapshot_t snapshot;
    if (pet_climate_get_snapshot(&snapshot) != ESP_OK) {
        return;
    }

    pet_climate_card_t card;
    pet_climate_card_render(&snapshot, &card);
    ESP_LOGI(TAG, "climate | %s | %s | %s | %s | %s | q=%u err=%u",
             card.temperature, card.humidity, card.comfort, card.trend, card.updated,
             reading.quality, reading.error_code);
}

static void on_climate_alert(const pet_event_t *event)
{
    pet_climate_alert_payload_t payload;
    if (event->payload_len < sizeof(payload)) {
        return;
    }
    memcpy(&payload, event->payload, sizeof(payload));
    ESP_LOGW(TAG, "climate alert: %s", alert_text(payload.alert));
}
#endif

esp_err_t app_start(void)
{
    ESP_LOGI(TAG, "reset_reason=%d, free_heap=%u, free_psram=%u",
             esp_reset_reason(),
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_ERROR_CHECK(pet_event_bus_init());
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_BOOT, on_boot));

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_READING, on_climate_reading));
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_ALERT, on_climate_alert));
#endif

#if CONFIG_PET_MODULE_GROWTH_ENABLED
    ESP_ERROR_CHECK(pet_growth_init());
#endif

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    ESP_ERROR_CHECK(pet_climate_init(pet_bsp_climate_sensor()));
    ESP_ERROR_CHECK(pet_climate_start());
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
    pet_ext_device_control_init();
#endif

#if CONFIG_PET_EXT_SERVO
    pet_ext_servo_init();
#endif

    pet_event_t boot;
    pet_event_prepare(&boot, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_NORMAL);
    boot.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ESP_ERROR_CHECK(pet_event_bus_publish(&boot));

    ESP_LOGI(TAG, "skeleton started, idf=%s", esp_get_idf_version());
    return ESP_OK;
}
