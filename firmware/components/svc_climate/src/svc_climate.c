#include "sdkconfig.h"

#if CONFIG_PET_MODULE_CLIMATE_ENABLED

#include "svc_climate/api.h"

#include <inttypes.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "climate_internal.h"
#include "pet/event_bus.h"
#include "pet/storage.h"

#define PET_CLIMATE_NVS_NAMESPACE "pet_climate"
#define PET_CLIMATE_NVS_KEY "state"
#define PET_CLIMATE_SAVE_INTERVAL_MS (5u * 60u * 1000u)
#define PET_CLIMATE_FAULT_RETRY_MS 30000u
#define PET_CLIMATE_TASK_STACK 3072
#define PET_CLIMATE_TASK_PRIORITY 2

static const char *TAG = "svc_climate";

static const pet_climate_sensor_t *s_sensor;
static pet_climate_pipeline_t s_pipeline;
static pet_climate_history_t s_history;
static pet_climate_alert_state_t s_alert;
static pet_climate_state_blob_t s_state;
static pet_climate_snapshot_t s_snapshot;
static pet_climate_chart_t s_chart;
static bool retry_requested;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_task;
static bool s_sensor_ready;
static bool s_fault;
static uint32_t s_failure_count;
static uint32_t s_last_save_ms;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void copy_offsets(float *temperature_offset_c, float *humidity_offset_pct)
{
    taskENTER_CRITICAL(&s_lock);
    *temperature_offset_c = (float)s_state.temperature_offset_centi / 100.0f;
    *humidity_offset_pct = (float)s_state.humidity_offset_centi / 100.0f;
    taskEXIT_CRITICAL(&s_lock);
}

static esp_err_t save_state(void)
{
    uint8_t buffer[sizeof(pet_climate_state_blob_t)];
    size_t size = 0;

    taskENTER_CRITICAL(&s_lock);
    const bool encoded = pet_climate_state_encode(&s_state, buffer, sizeof(buffer), &size);
    taskEXIT_CRITICAL(&s_lock);

    if (!encoded) {
        return ESP_ERR_INVALID_STATE;
    }
    return pet_storage_save(PET_CLIMATE_NVS_NAMESPACE, PET_CLIMATE_NVS_KEY, buffer, size);
}

static void publish_reading(const pet_climate_reading_t *reading)
{
    pet_event_t event;
    pet_event_prepare(&event, PET_EVT_CLIMATE_READING, PET_EVENT_SOURCE_CLIMATE,
                      PET_EVENT_PRIORITY_NORMAL);
    event.timestamp_ms = reading->timestamp_ms;
    memcpy(event.payload, reading, sizeof(*reading));
    event.payload_len = (uint16_t)sizeof(*reading);
    pet_event_bus_publish(&event);
}

static void publish_alert(pet_climate_alert_t alert, const pet_climate_reading_t *reading)
{
    const pet_climate_alert_payload_t payload = {
        .timestamp_ms = reading->timestamp_ms,
        .alert = alert,
        .temperature_c = reading->temperature_c,
        .humidity_pct = reading->humidity_pct,
    };

    pet_event_t event;
    pet_event_prepare(&event, PET_EVT_CLIMATE_ALERT, PET_EVENT_SOURCE_CLIMATE,
                      PET_EVENT_PRIORITY_HIGH);
    event.timestamp_ms = payload.timestamp_ms;
    memcpy(event.payload, &payload, sizeof(payload));
    event.payload_len = (uint16_t)sizeof(payload);
    pet_event_bus_publish(&event);
}

static void update_snapshot(const pet_climate_reading_t *reading, uint32_t timestamp_ms)
{
    pet_climate_history_summary_t summary;
    pet_climate_history_summary(&s_history, timestamp_ms, &summary);

    pet_climate_chart_t chart;
    pet_climate_history_chart(&s_history,timestamp_ms,&chart);
    taskENTER_CRITICAL(&s_lock);
    s_chart=chart;
    const uint32_t age_ms = s_pipeline.has_valid && timestamp_ms >= s_pipeline.last_valid_ms
                                ? timestamp_ms - s_pipeline.last_valid_ms
                                : 0;
    s_snapshot.reading = *reading;
    s_snapshot.status = s_fault ? PET_CLIMATE_STATUS_FAULT
                                : pet_climate_pipeline_status(&s_pipeline, timestamp_ms);
    s_snapshot.alert = s_alert.active;
    s_snapshot.trend_c_per_10min = summary.trend_c_per_10min;
    s_snapshot.trend_rh_per_10min = summary.trend_rh_per_10min;
    s_snapshot.age_ms = age_ms;
    s_snapshot.failure_count = s_failure_count;
    taskEXIT_CRITICAL(&s_lock);
}

static pet_climate_error_t map_sensor_error(esp_err_t err)
{
    switch (err) {
    case ESP_OK:
        return PET_CLIMATE_ERR_NONE;
    case ESP_ERR_TIMEOUT:
        return PET_CLIMATE_ERR_TIMEOUT;
    case ESP_ERR_INVALID_CRC:
        return PET_CLIMATE_ERR_CHECKSUM;
    case ESP_ERR_INVALID_ARG:
        return PET_CLIMATE_ERR_RANGE;
    default:
        return PET_CLIMATE_ERR_FAULT;
    }
}

static void climate_task(void *arg)
{
    (void)arg;
    uint32_t period_ms = CONFIG_PET_CLIMATE_SAMPLE_PERIOD_MS;

    for (;;) {
        const uint32_t timestamp_ms = now_ms();
        pet_climate_reading_t raw = {0};
        esp_err_t read_error = ESP_ERR_INVALID_STATE;

        if (!s_sensor_ready) {
            s_sensor_ready = (s_sensor->init() == ESP_OK);
        }
        if (s_sensor_ready) {
            read_error = s_sensor->read(&raw);
            if (read_error == ESP_ERR_INVALID_STATE) {
                s_sensor_ready = false;
            }
        }

        float temperature_offset_c = 0.0f;
        float humidity_offset_pct = 0.0f;
        copy_offsets(&temperature_offset_c, &humidity_offset_pct);

        const pet_climate_error_t error = map_sensor_error(read_error);
        pet_climate_reading_t reading =
            pet_climate_pipeline_push(&s_pipeline, error, raw.temperature_c, raw.humidity_pct,
                                      timestamp_ms, temperature_offset_c, humidity_offset_pct);

        if (reading.valid) {
            s_failure_count = 0;
            s_fault = false;
            pet_climate_history_push(&s_history, &reading);
            taskENTER_CRITICAL(&s_lock);
            pet_climate_state_update(&s_state, &reading);
            taskEXIT_CRITICAL(&s_lock);

            const pet_climate_alert_t alert =
                pet_climate_alert_update(&s_alert, true, reading.temperature_c,
                                         reading.humidity_pct, timestamp_ms);
            if (alert != PET_CLIMATE_ALERT_NONE) {
                publish_alert(alert, &reading);
            }

            if (s_last_save_ms == 0 || timestamp_ms - s_last_save_ms >= PET_CLIMATE_SAVE_INTERVAL_MS) {
                if (save_state() == ESP_OK) {
                    s_last_save_ms = timestamp_ms;
                } else {
                    ESP_LOGW(TAG, "state save failed, retry next cycle");
                }
            }
        } else if (error != PET_CLIMATE_ERR_SPIKE) {
            if (s_failure_count < UINT32_MAX) {
                s_failure_count++;
            }
            if (s_failure_count >= 3 && !s_fault) {
                s_fault = true;
                ESP_LOGW(TAG, "sensor fault after %u failures", (unsigned)s_failure_count);
            }
        }

        update_snapshot(&reading, timestamp_ms);
        publish_reading(&reading);

        const uint32_t delay_ms = s_fault ? PET_CLIMATE_FAULT_RETRY_MS : period_ms;
        /* 先满足 DHT 的物理最短间隔；重试请求只缩短故障退避。 */
        vTaskDelay(pdMS_TO_TICKS(2000));
        for (uint32_t waited=2000;waited<delay_ms;waited+=100) {
            taskENTER_CRITICAL(&s_lock);bool retry=retry_requested;retry_requested=false;taskEXIT_CRITICAL(&s_lock);
            if (retry) break;
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

esp_err_t pet_climate_init(const pet_climate_sensor_t *sensor)
{
    if (sensor == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = pet_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "storage init failed: %s", esp_err_to_name(err));
        return err;
    }

    pet_climate_state_defaults(&s_state);

    pet_climate_state_blob_t loaded;
    size_t size = 0;
    if (pet_storage_load(PET_CLIMATE_NVS_NAMESPACE, PET_CLIMATE_NVS_KEY, &loaded,
                         sizeof(loaded), &size) == ESP_OK &&
        pet_climate_state_decode(&loaded, size, &s_state)) {
        ESP_LOGI(TAG, "state loaded: day=%" PRIu32 ", temp_offset=%d centi, humidity_offset=%d centi",
                 s_state.day_index,
                 s_state.temperature_offset_centi,
                 s_state.humidity_offset_centi);
    }

    pet_climate_pipeline_reset(&s_pipeline);
    pet_climate_history_reset(&s_history);
    pet_climate_alert_reset(&s_alert);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.status = PET_CLIMATE_STATUS_NO_DATA;

    s_sensor = sensor;
    s_failure_count = 0;
    s_fault = false;
    s_last_save_ms = 0;
    s_sensor_ready = (sensor->init() == ESP_OK);
    if (!s_sensor_ready) {
        ESP_LOGW(TAG, "sensor %s init failed, will retry", sensor->id);
        s_fault = true;
    }

    ESP_LOGI(TAG, "module ready: sensor=%s", sensor->id);
    return ESP_OK;
}

esp_err_t pet_climate_start(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    if (s_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xTaskCreate(climate_task, "climate", PET_CLIMATE_TASK_STACK, NULL,
                    PET_CLIMATE_TASK_PRIORITY, &s_task) != pdPASS) {
        s_task = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t pet_climate_stop(void)
{
    if (s_task == NULL) {
        return ESP_OK;
    }
    vTaskDelete(s_task);
    s_task = NULL;
    return ESP_OK;
}

esp_err_t pet_climate_get_snapshot(pet_climate_snapshot_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_lock);
    *out = s_snapshot;
    taskEXIT_CRITICAL(&s_lock);
    if (out->reading.valid) {
        out->age_ms=now_ms()-out->reading.timestamp_ms;
        if (out->status==PET_CLIMATE_STATUS_OK&&out->age_ms>PET_CLIMATE_STALE_AFTER_MS)
            out->status=PET_CLIMATE_STATUS_STALE;
    }
    return ESP_OK;
}

esp_err_t pet_climate_set_calibration(float temperature_offset_c, float humidity_offset_pct)
{
    if (temperature_offset_c < -5.0f || temperature_offset_c > 5.0f ||
        humidity_offset_pct < -20.0f || humidity_offset_pct > 20.0f) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_sensor == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    taskENTER_CRITICAL(&s_lock);
    s_state.temperature_offset_centi = (int16_t)(temperature_offset_c * 100.0f);
    s_state.humidity_offset_centi = (int16_t)(humidity_offset_pct * 100.0f);
    taskEXIT_CRITICAL(&s_lock);

    return save_state();
}

esp_err_t pet_climate_get_chart(pet_climate_chart_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!s_sensor) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_lock);*out=s_chart;taskEXIT_CRITICAL(&s_lock);return ESP_OK;
}
esp_err_t pet_climate_retry(void)
{
    if (!s_task) return ESP_ERR_INVALID_STATE;
    taskENTER_CRITICAL(&s_lock);retry_requested=true;taskEXIT_CRITICAL(&s_lock);return ESP_OK;
}
#endif
