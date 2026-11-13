#pragma once

#include <stddef.h>
#include "pet/message.h"

typedef enum {
    PET_MESSAGE_SCREEN_NONE,
    PET_MESSAGE_SCREEN_SHOW,
    PET_MESSAGE_SCREEN_HIDE,
    PET_MESSAGE_SCREEN_READ,
} pet_message_screen_action_kind_t;

typedef struct {
    pet_message_screen_action_kind_t kind;
    char id[PET_MESSAGE_ID_MAX + 1];
    size_t index;
    uint32_t generation;
    bool plain_text;
} pet_message_screen_action_t;

/* 单 UI 线程拥有；仅存消息 ID，不复制队列正文。 */
typedef struct {
    char id[PET_MESSAGE_ID_MAX + 1];
    uint32_t generation;
    uint32_t render_started_ms;
    uint32_t visible_since_ms;
    uint32_t duration_ms;
    bool rendering;
    bool visible;
    bool plain_text;
    bool retry_pending;
    bool retry_wait;
    bool needs_hide;
    uint32_t failed_at_ms;
    char seen[PET_MESSAGE_QUEUE_CAPACITY][PET_MESSAGE_ID_MAX + 1];
    size_t seen_count;
    bool manual;
    bool browse_requested;
    bool button_initialized;
    bool button_raw;
    bool button_stable;
    bool button_armed;
    bool button_target_idle;
    uint32_t button_changed_ms;
    uint32_t button_pressed_ms;
    uint32_t button_generation;
} pet_message_presenter_t;

void pet_message_presenter_init(pet_message_presenter_t *presenter);

/* messages 为按优先级/时间排序的只读快照；epoch=0 表示尚未对时。 */
pet_message_screen_action_t pet_message_presenter_tick(pet_message_presenter_t *presenter,
                                                       const pet_message_view_t *messages,
                                                       size_t count, uint32_t now_ms,
                                                       uint32_t now_epoch_s, bool dnd,
                                                       bool allow_critical);

/* 活动语音/手势暂缓新普通弹窗；critical 仍立即抢占，已显示消息和回执规则不变。 */
pet_message_screen_action_t pet_message_presenter_tick_focused(pet_message_presenter_t *presenter,
    const pet_message_view_t *messages, size_t count, uint32_t now_ms, uint32_t now_epoch_s,
    bool dnd, bool allow_critical, bool voice_focused);

/* success 必须代表整帧传输完成；只对当前代首次成功返回 true（可发送 displayed）。 */
bool pet_message_presenter_rendered(pet_message_presenter_t *presenter, uint32_t generation,
                                    bool success, uint32_t now_ms);

/* 原始按键电平，每 10 ms 采样；30 ms 消抖、800 ms 以上长按忽略。
 * 按下时绑定可见帧，松开时不允许确认随后抢占的新消息。空闲短按打开未读。 */
pet_message_screen_action_t pet_message_presenter_button(pet_message_presenter_t *presenter,
                                                         bool pressed, uint32_t now_ms);
/* 仅在消息中心成功接受 read 后清除当前画面；失败时允许再次确认。 */
bool pet_message_presenter_read_complete(pet_message_presenter_t *presenter, uint32_t generation);
/* 后续滚动帧失败/黑屏时立即取消可读状态，下一次 tick 回退重绘。 */
void pet_message_presenter_display_lost(pet_message_presenter_t *presenter);
