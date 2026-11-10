#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pet/event.h"

/* 温湿度跨模块契约：纯 C，不依赖 ESP-IDF，可在主机测试中直接编译。 */

#define PET_CLIMATE_SCHEMA_VERSION 1

/* 判定阈值；校准偏移在采样链内叠加，这里只定义原始量与输出范围。 */
#define PET_CLIMATE_TEMP_MIN_C (-10.0f)
#define PET_CLIMATE_TEMP_MAX_C (60.0f)
#define PET_CLIMATE_HUMIDITY_MIN_PCT (0.0f)
#define PET_CLIMATE_HUMIDITY_MAX_PCT (100.0f)

typedef enum {
    PET_CLIMATE_ERR_NONE = 0,
    PET_CLIMATE_ERR_TIMEOUT,
    PET_CLIMATE_ERR_CHECKSUM,
    PET_CLIMATE_ERR_RANGE,
    PET_CLIMATE_ERR_SPIKE,
    PET_CLIMATE_ERR_STALE,
    PET_CLIMATE_ERR_FAULT,
    PET_CLIMATE_ERR_STORAGE,
} pet_climate_error_t;

typedef enum {
    PET_CLIMATE_STATUS_NO_DATA = 0,
    PET_CLIMATE_STATUS_OK,
    PET_CLIMATE_STATUS_STALE,
    PET_CLIMATE_STATUS_FAULT,
} pet_climate_status_t;

typedef enum {
    PET_CLIMATE_ALERT_NONE = 0,
    PET_CLIMATE_ALERT_COLD,
    PET_CLIMATE_ALERT_HOT,
    PET_CLIMATE_ALERT_DRY,
    PET_CLIMATE_ALERT_HUMID,
    PET_CLIMATE_ALERT_CONDENSATION,
} pet_climate_alert_t;

typedef struct {
    float temperature_c;
    float humidity_pct;
    uint32_t timestamp_ms;
    uint16_t error_code; /* pet_climate_error_t */
    uint8_t quality;     /* 0–100，滤波后置信度 */
    bool valid;
} pet_climate_reading_t;

typedef struct {
    uint32_t timestamp_ms;
    pet_climate_alert_t alert;
    float temperature_c;
    float humidity_pct;
} pet_climate_alert_payload_t;

typedef struct {
    pet_climate_reading_t reading;
    pet_climate_status_t status;
    pet_climate_alert_t alert;
    float trend_c_per_10min;
    float trend_rh_per_10min;
    uint32_t age_ms;
    uint32_t failure_count;
} pet_climate_snapshot_t;

/* 事件 payload 固定内联，结构体必须放得下。 */
_Static_assert(sizeof(pet_climate_reading_t) <= PET_EVENT_PAYLOAD_MAX,
               "climate reading must fit in pet_event_t payload");
_Static_assert(sizeof(pet_climate_alert_payload_t) <= PET_EVENT_PAYLOAD_MAX,
               "climate alert must fit in pet_event_t payload");
