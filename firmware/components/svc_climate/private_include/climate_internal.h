#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet/climate.h"

/* 采样链常量与纯逻辑声明；本头文件不依赖 ESP-IDF，主机测试直接编译。 */

#define PET_CLIMATE_STALE_AFTER_MS 30000u
#define PET_CLIMATE_SPIKE_TEMP_C 5.0f
#define PET_CLIMATE_SPIKE_HUMIDITY_PCT 20.0f
#define PET_CLIMATE_ALERT_DEBOUNCE_MS (10u * 60u * 1000u)
#define PET_CLIMATE_ALERT_RECOVERY_COUNT 3
#define PET_CLIMATE_HISTORY_CAPACITY 720u /* 5 s × 720 = 60 min */
#define PET_CLIMATE_HISTORY_WINDOW_MS (60u * 60u * 1000u)
#define PET_CLIMATE_TREND_WINDOW_MS (10u * 60u * 1000u)

#define PET_CLIMATE_STATE_MAGIC 0x434C494DUL /* 'CLIM' */
#define PET_CLIMATE_STATE_VERSION 1
#define PET_CLIMATE_MS_PER_DAY 86400000u

typedef struct {
    float temperature_median[3];
    float humidity_median[3];
    uint8_t temperature_median_count;
    uint8_t temperature_median_head;
    uint8_t humidity_median_count;
    uint8_t humidity_median_head;

    float temperature_average[5];
    float humidity_average[5];
    uint8_t temperature_average_count;
    uint8_t temperature_average_head;
    uint8_t humidity_average_count;
    uint8_t humidity_average_head;

    float last_temperature_c;
    float last_humidity_pct;
    uint32_t last_valid_ms;
    bool has_valid;
    uint8_t quality;
    uint8_t spike_streak;
} pet_climate_pipeline_t;

void pet_climate_pipeline_reset(pet_climate_pipeline_t *pipeline);

/* 单次采样入链；error != NONE 表示读取失败，仍更新质量分并返回无效读数。 */
pet_climate_reading_t pet_climate_pipeline_push(pet_climate_pipeline_t *pipeline,
                                                pet_climate_error_t error,
                                                float temperature_c,
                                                float humidity_pct,
                                                uint32_t now_ms,
                                                float temperature_offset_c,
                                                float humidity_offset_pct);

pet_climate_status_t pet_climate_pipeline_status(const pet_climate_pipeline_t *pipeline,
                                                 uint32_t now_ms);

/* 60 分钟环形缓冲。 */
typedef struct {
    int16_t temperature_centi;
    int16_t humidity_centi;
    uint32_t timestamp_ms;
} pet_climate_history_point_t;

typedef struct {
    pet_climate_history_point_t points[PET_CLIMATE_HISTORY_CAPACITY];
    uint16_t head;
    uint16_t count;
} pet_climate_history_t;

typedef struct {
    bool valid;
    float temperature_min_c;
    float temperature_max_c;
    float temperature_avg_c;
    float humidity_min_pct;
    float humidity_max_pct;
    float humidity_avg_pct;
    float trend_c_per_10min;
    float trend_rh_per_10min;
} pet_climate_history_summary_t;

void pet_climate_history_reset(pet_climate_history_t *history);
void pet_climate_history_push(pet_climate_history_t *history, const pet_climate_reading_t *reading);
void pet_climate_history_summary(const pet_climate_history_t *history, uint32_t now_ms,
                                 pet_climate_history_summary_t *out);

/* 告警防抖：同一告警 10 分钟内不重复，恢复需连续 3 次正常读数。 */
typedef struct {
    pet_climate_alert_t active;
    uint32_t last_emit_ms;
    uint8_t normal_streak;
    float previous_temperature_c;
    bool has_previous;
} pet_climate_alert_state_t;

void pet_climate_alert_reset(pet_climate_alert_state_t *state);

/* 返回本次应发布的告警；PET_CLIMATE_ALERT_NONE 表示不发布。 */
pet_climate_alert_t pet_climate_alert_update(pet_climate_alert_state_t *state,
                                             bool valid, float temperature_c,
                                             float humidity_pct, uint32_t now_ms);

/* 每日极值与校准偏移的持久化结构。 */
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t day_index;
    int16_t temperature_min_centi;
    int16_t temperature_max_centi;
    int16_t humidity_min_centi;
    int16_t humidity_max_centi;
    int16_t temperature_offset_centi;
    int16_t humidity_offset_centi;
    uint32_t crc32;
} pet_climate_state_blob_t;

_Static_assert(sizeof(pet_climate_state_blob_t) == 28, "climate state blob layout changed");

void pet_climate_state_defaults(pet_climate_state_blob_t *state);

/* 有效读数才更新极值；跨天时重置当日极值。 */
void pet_climate_state_update(pet_climate_state_blob_t *state, const pet_climate_reading_t *reading);

bool pet_climate_state_encode(const pet_climate_state_blob_t *state, void *out,
                              size_t capacity, size_t *out_size);
bool pet_climate_state_decode(const void *data, size_t size, pet_climate_state_blob_t *out);

uint32_t pet_climate_crc32(const void *data, size_t size);

void pet_climate_history_chart(const pet_climate_history_t *history,uint32_t now_ms,pet_climate_chart_t *out);
