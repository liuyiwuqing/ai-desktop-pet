#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 跨模块事件契约：纯 C，不依赖 ESP-IDF，可在主机测试中直接编译。 */

#define PET_EVENT_PAYLOAD_MAX 32

typedef enum {
    PET_EVT_BOOT = 0,
    PET_EVT_TICK,
    PET_EVT_FAULT,
    PET_EVT_COUNT,
} pet_event_id_t;

typedef struct {
    pet_event_id_t id;
    uint32_t timestamp_ms;
    uint16_t payload_len;
    uint8_t payload[PET_EVENT_PAYLOAD_MAX]; /* 固定内联，禁止跨任务裸指针 */
} pet_event_t;

/* S1/S2 预算：事件保持小体积并内联，扩容需同步评审队列深度与堆占用。 */
_Static_assert(sizeof(pet_event_t) <= 48, "pet_event_t must stay within 48 B");

static inline bool pet_event_is_valid(const pet_event_t *event)
{
    return event != NULL && event->id >= PET_EVT_BOOT && event->id < PET_EVT_COUNT &&
           event->payload_len <= PET_EVENT_PAYLOAD_MAX;
}
