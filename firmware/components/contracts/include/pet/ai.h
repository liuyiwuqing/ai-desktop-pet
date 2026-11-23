#pragma once

#include <stdbool.h>
#include <stdint.h>

#define PET_AI_MIN_SPEECH_MS 300u
#define PET_AI_END_SILENCE_MS 300u
#define PET_AI_MAX_LISTEN_MS 15000u
#define PET_AI_RESPONSE_TIMEOUT_MS 15000u
#define PET_AI_FRAME_GAP_MS 100u
#define PET_AI_WAKE_COOLDOWN_MS 3000u
/* M1 连续对话：回复播完后保持的续轮窗口与总量上限（防误触发耗流量）。 */
#define PET_AI_CONV_WINDOW_MS 10000u
#define PET_AI_CONV_RESUME_MS 200u
#define PET_AI_CONV_MAX_ROUNDS 20u
#define PET_AI_CONV_TOTAL_MS 300000u

typedef enum {
    PET_AI_IDLE,
    PET_AI_LISTENING,
    PET_AI_THINKING,
    PET_AI_SPEAKING,
    PET_AI_CONVERSATION, /* 回复播完后的续轮窗口：保持连接与采集，VAD 检测语音续轮。 */
    PET_AI_MUTED,
    PET_AI_ERROR,
    PET_AI_STATE_COUNT,
} pet_ai_state_t;

typedef enum {
    PET_AI_REASON_NONE,
    PET_AI_REASON_WAKE,
    PET_AI_REASON_PRESS,
    PET_AI_REASON_SILENCE,
    PET_AI_REASON_RELEASE,
    PET_AI_REASON_LIMIT,
    PET_AI_REASON_NO_SPEECH,
    PET_AI_REASON_RESPONSE,
    PET_AI_REASON_DRAINED,
    PET_AI_REASON_CANCELLED,
    PET_AI_REASON_MUTED,
    PET_AI_REASON_UNMUTED,
    PET_AI_REASON_RESPONSE_TIMEOUT,
    PET_AI_REASON_INPUT_GAP,
    PET_AI_REASON_AUDIO_ERROR,
    PET_AI_REASON_TRANSPORT_ERROR,
    PET_AI_REASON_CONV_SPEECH, /* 会话窗口内检测到语音，续开一轮。 */
    PET_AI_REASON_CONV_TIMEOUT, /* 会话窗口超时，回待机。 */
} pet_ai_reason_t;

/* 逻辑会话快照；状态描述控制意图，不能据此宣称物理麦克风/功放已启停。 */
typedef struct {
    uint32_t turn_id; /* 当前生命周期内递增，0 表示尚未开启过一轮。 */
    pet_ai_state_t state;
    pet_ai_reason_t reason;
    uint16_t speech_ms;
    uint16_t silence_ms;
    uint8_t level_pct;
    bool push_to_talk;
} pet_ai_view_t;

#define PET_AI_TEXT_MAX 240u
typedef enum { PET_AI_TEXT_USER, PET_AI_TEXT_ASSISTANT } pet_ai_text_role_t;
