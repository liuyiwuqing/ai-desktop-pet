#pragma once

#include <stddef.h>

#include "pet/ai.h"
#include "pet/audio.h"

typedef enum {
    PET_AI_OK,
    PET_AI_IGNORED,
    PET_AI_STALE,
    PET_AI_INVALID,
    PET_AI_NOT_READY,
    PET_AI_ID_EXHAUSTED,
} pet_ai_result_t;

typedef enum {
    PET_AI_WAKE_DETECTED, /* 来自唤醒检测器，本接口不识别唤醒词。 */
    PET_AI_TALK_PRESS,    /* 仅语义输入，不绑定 BOOT。 */
    PET_AI_TALK_RELEASE,
    PET_AI_RESPONSE_BEGIN,
    PET_AI_PLAYBACK_DRAINED, /* 仅在输入已结束且最后一段真实 DMA drain 完成后发送。 */
    PET_AI_CANCEL,
    PET_AI_MUTE,
    PET_AI_UNMUTE,
    PET_AI_AUDIO_FAILED,
    PET_AI_TRANSPORT_FAILED,
    PET_AI_TICK,
    PET_AI_INPUT_COUNT,
} pet_ai_input_kind_t;

typedef struct {
    pet_ai_input_kind_t kind;
    uint32_t turn_id; /* release/response/drained/failed 必须携带原轮次，其余必须为 0。 */
    uint64_t now_ms;  /* 同一拥有者的单调时钟；不能使用墙上时间。 */
} pet_ai_input_t;

typedef enum {
    PET_AI_ACTIVITY_ENERGY, /* 无模型时使用带滞回的 RMS 阈值；不代表识别到了人声。 */
    PET_AI_ACTIVITY_SPEECH,
    PET_AI_ACTIVITY_SILENCE,
} pet_ai_activity_t;

typedef struct {
    uint32_t turn_id;
    uint64_t now_ms;
    const int16_t *pcm; /* 同步消费，不保存；16 kHz mono signed 16-bit。 */
    size_t samples;     /* 必须为一个 20 ms 帧（320 样本）。 */
    pet_ai_activity_t activity; /* 模型可显式提供 VAD 标签，端点规则不依赖模型实现。 */
} pet_ai_audio_input_t;

enum {
    PET_AI_ACTION_STATE_CHANGED = 1u << 0,
    PET_AI_ACTION_BEGIN_TURN = 1u << 1,
    PET_AI_ACTION_START_CAPTURE = 1u << 2,
    PET_AI_ACTION_STOP_CAPTURE = 1u << 3,
    PET_AI_ACTION_FORWARD_PCM = 1u << 4,
    PET_AI_ACTION_END_INPUT = 1u << 5,
    PET_AI_ACTION_CANCEL_TURN = 1u << 6,
    PET_AI_ACTION_START_PLAYBACK = 1u << 7,
    PET_AI_ACTION_STOP_PLAYBACK = 1u << 8,
    PET_AI_ACTION_CLEAR_AUDIO = 1u << 9,
};

typedef struct {
    pet_ai_view_t view;
    uint32_t actions;
    uint32_t cancelled_turn_id;
} pet_ai_step_t;

typedef struct {
    uint16_t energy_on_rms;  /* 默认 800；仅桌面/半双工回退起点，实机需校准。 */
    uint16_t energy_off_rms; /* 默认 400；必须小于 on，避免门槛附近反复翻转。 */
    uint16_t conversation_window_ms; /* 0 禁用连续对话；启用时回复播完后保持该窗口等待续轮。 */
    uint8_t conversation_max_rounds; /* 会话内最多轮次（每完成一次播报计 1）；0 视为无效配置。 */
} pet_ai_session_config_t;

/* 调用方持有存储；仅由下面 API 修改，单一任务串行拥有，不跨任务共享裸结构。 */
typedef struct {
    pet_ai_view_t view;
    pet_ai_session_config_t config;
    uint64_t last_now_ms, listen_start_ms, last_frame_ms, state_start_ms, last_wake_ms;
    uint64_t conv_start_ms; /* 会话起点；窗口内统计轮次与总时长，退出后归零重开。 */
    uint32_t conv_rounds;
    bool ready, energy_speech, have_wake, conv_active;
} pet_ai_session_t;

/* config=NULL 使用默认值。重新 init 前必须停止旧音频、丢弃全部旧异步事件。 */
pet_ai_result_t pet_ai_session_init(pet_ai_session_t *session,
                                   const pet_ai_session_config_t *config, uint64_t now_ms);
bool pet_ai_session_snapshot(const pet_ai_session_t *session, pet_ai_view_t *view);
pet_ai_result_t pet_ai_session_handle(pet_ai_session_t *session,
                                     const pet_ai_input_t *input, pet_ai_step_t *step);
pet_ai_result_t pet_ai_session_audio(pet_ai_session_t *session,
                                    const pet_ai_audio_input_t *input, pet_ai_step_t *step);

/*
 * 这里只生成控制意图，不执行 HAL、网络、UI 或存储。消费 actions 时先处理旧轮次的
 * cancel/stop/clear，再 begin/start；同帧先 forward 再 end_input。HAL/传输失败必须
 * 回送携带 turn_id 的 failed 输入，不能将意图当作执行成功或静音证据。
 */
