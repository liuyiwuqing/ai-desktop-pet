#include "sdkconfig.h"

#if CONFIG_PET_MODULE_GROWTH_ENABLED

#include <time.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "pet/event_bus.h"
#include "svc_growth/api.h"

static bool started;
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
static QueueHandle_t climate_updates;
static void on_climate(const pet_event_t *event)
{
    if (climate_updates && event->id == PET_EVT_CLIMATE_READING &&
        event->source == PET_EVENT_SOURCE_CLIMATE && event->payload_len == sizeof(pet_climate_reading_t)) {
        /* 事件任务只复制最新读数；Flash 结算由成长工作任务承担。 */
        xQueueOverwrite(climate_updates, event->payload);
    }
}
#endif
static uint64_t monotonic(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time() / 1000;
}
static uint32_t epoch(void *context)
{
    (void)context;
    time_t value = time(NULL);
    /* 不依赖网络组件；系统尚未对时时保留离线存档，等待时间源建立。 */
    return value >= 1577836800 && (uint64_t)value <= UINT32_MAX ? (uint32_t)value : 0;
}
static void growth_task(void *context)
{
    (void)context;
    /* 订阅/资源初始化全部成功后才放行，失败清理不会打断正在进行的存档。 */
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    pet_growth_result_t previous = PET_GROWTH_OK;
    for (;;) {
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        pet_climate_reading_t reading;
        if (xQueueReceive(climate_updates, &reading, 0) == pdTRUE)
            (void)pet_growth_observe_climate(&reading);
#endif
        pet_growth_result_t result = pet_growth_tick();
        if (result != previous && result != PET_GROWTH_OK)
            ESP_LOGW("svc_growth", "settlement: %s", pet_growth_result_name(result));
        previous = result;
        vTaskDelay(pdMS_TO_TICKS(result == PET_GROWTH_STORAGE_ERROR ||
                               result == PET_GROWTH_STORAGE_UNCERTAIN ? 300000 : 1000));
    }
}

esp_err_t pet_growth_start(void)
{
    if (started) return ESP_OK;
    const pet_growth_clock_t clock = {
        .monotonic_ms = monotonic, .epoch_s = epoch,
        .utc_offset_min = CONFIG_PET_GROWTH_UTC_OFFSET_MINUTES,
    };
    pet_growth_result_t result = pet_growth_init(&clock);
    if (result != PET_GROWTH_OK) {
        ESP_LOGW("svc_growth", "init: %s; stored data preserved", pet_growth_result_name(result));
        return ESP_FAIL;
    }
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    climate_updates = xQueueCreate(1, sizeof(pet_climate_reading_t));
    if (!climate_updates) { pet_growth_close(); return ESP_ERR_NO_MEM; }
#endif
    TaskHandle_t task = NULL;
    if (xTaskCreate(growth_task, "pet_growth", 4096, NULL, 2, &task) != pdPASS) {
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        vQueueDelete(climate_updates);
        climate_updates = NULL;
#endif
        pet_growth_close();
        return ESP_ERR_NO_MEM;
    }
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    esp_err_t err = pet_event_bus_subscribe(PET_EVT_CLIMATE_READING, on_climate);
    if (err != ESP_OK) {
        vTaskDelete(task);
        vQueueDelete(climate_updates);
        climate_updates = NULL;
        pet_growth_close();
        return err;
    }
#endif
    started = true;
    xTaskNotifyGive(task);
    ESP_LOGI("svc_growth", "local growth service ready");
    return ESP_OK;
}
#endif
