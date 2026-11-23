#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PET_KEY_COUNT 3u

/* 消抖与判定阈值（与 BOOT 按键约定一致）：短按 <800ms、长按 >=800ms、
 * 组合=两键按下起点重叠窗口 300ms 内。 */
#define PET_KEY_DEBOUNCE_MS 30u
#define PET_KEY_LONG_PRESS_MS 800u
#define PET_KEY_COMBO_WINDOW_MS 300u

typedef enum { PET_KEY_CONFIRM = 0, PET_KEY_NEXT = 1, PET_KEY_VOICE = 2 } pet_key_id_t;

typedef enum {
    PET_KEY_EVENT_NONE = 0,
    PET_KEY_EVENT_SHORT_PRESS,
    PET_KEY_EVENT_LONG_PRESS,
    PET_KEY_EVENT_COMBO_CONFIRM_NEXT, /* KEY_A+KEY_B：重启提示。 */
    PET_KEY_EVENT_COMBO_NEXT_VOICE    /* KEY_B+KEY_C：息屏开关。 */
} pet_key_event_kind_t;

typedef struct { uint8_t kind; uint8_t key; } pet_key_event_t;

typedef struct {
    bool stable[PET_KEY_COUNT], sample[PET_KEY_COUNT];
    uint32_t sample_ms[PET_KEY_COUNT], press_ms[PET_KEY_COUNT];
    bool long_fired[PET_KEY_COUNT];
    uint8_t combo_keys; /* 组合待定时的两键位掩码；0=无。 */
} pet_key_tracker_t;

void pet_key_tracker_init(pet_key_tracker_t *tracker);

/* 每个 UI tick 喂入原始电平（active-low 已转 bool）；产生的事件写入 out（最多
 * capacity 个，超出的丢弃），返回产生的事件总数。一次调用最多 3 个事件。 */
unsigned pet_key_tracker_update(pet_key_tracker_t *tracker,
    const bool pressed[PET_KEY_COUNT], uint32_t now,
    pet_key_event_t *out, unsigned capacity);
