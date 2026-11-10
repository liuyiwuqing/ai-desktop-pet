#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* 跨模块事件契约：纯 C，不依赖 ESP-IDF，可在主机测试中直接编译。 */

#define PET_EVENT_PAYLOAD_MAX 64
#define PET_EVENT_SCHEMA_VERSION 1

typedef enum {
    PET_EVT_BOOT = 0,
    PET_EVT_TICK,
    PET_EVT_FAULT,
    PET_EVT_CLIMATE_READING,
    PET_EVT_CLIMATE_ALERT,
    PET_EVT_MSG_RECEIVED,
    PET_EVT_MSG_DISPLAYED,
    PET_EVT_MSG_READ,
    PET_EVT_COUNT,
} pet_event_id_t;

typedef enum {
    PET_EVENT_PRIORITY_LOW = 0,
    PET_EVENT_PRIORITY_NORMAL,
    PET_EVENT_PRIORITY_HIGH,
    PET_EVENT_PRIORITY_CRITICAL,
    PET_EVENT_PRIORITY_COUNT,
} pet_event_priority_t;

typedef enum {
    PET_EVENT_SOURCE_UNKNOWN = 0,
    PET_EVENT_SOURCE_APP,
    PET_EVENT_SOURCE_CORE,
    PET_EVENT_SOURCE_AI,
    PET_EVENT_SOURCE_MESSAGE,
    PET_EVENT_SOURCE_CLIMATE,
    PET_EVENT_SOURCE_GROWTH,
    PET_EVENT_SOURCE_UI,
    PET_EVENT_SOURCE_EXTENSION,
    PET_EVENT_SOURCE_COUNT,
} pet_event_source_t;

typedef struct {
    uint32_t timestamp_ms;
    uint32_t correlation_id; /* 同一次交互/消息链路共享；0 表示未关联 */
    uint32_t sequence;       /* 发布时由事件总线赋值，单调递增 */
    pet_event_id_t id;
    uint16_t schema_version;
    uint16_t payload_len;
    pet_event_priority_t priority;
    pet_event_source_t source;
    uint8_t payload[PET_EVENT_PAYLOAD_MAX]; /* 固定内联，禁止跨任务裸指针 */
} pet_event_t;

/* S3 预算：64 B 内联 payload + 路由字段；扩容需同步评审队列深度与堆占用。 */
_Static_assert(sizeof(pet_event_t) <= 96, "pet_event_t must stay within 96 B");

static inline void pet_event_prepare(pet_event_t *event, pet_event_id_t id,
                                     pet_event_source_t source, pet_event_priority_t priority)
{
    memset(event, 0, sizeof(*event));
    event->id = id;
    event->source = source;
    event->priority = priority;
    event->schema_version = PET_EVENT_SCHEMA_VERSION;
}

static inline bool pet_event_is_valid(const pet_event_t *event)
{
    return event != NULL && event->id >= PET_EVT_BOOT && event->id < PET_EVT_COUNT &&
           event->payload_len <= PET_EVENT_PAYLOAD_MAX &&
           event->schema_version == PET_EVENT_SCHEMA_VERSION &&
           event->priority >= PET_EVENT_PRIORITY_LOW &&
           event->priority < PET_EVENT_PRIORITY_COUNT &&
           event->source >= PET_EVENT_SOURCE_UNKNOWN && event->source < PET_EVENT_SOURCE_COUNT;
}
