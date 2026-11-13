#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "svc_ai_agent/session.h"

/*
 * 运行任务的外部端口只返回“是否已接受”，不把接受写入 DMA/网络误报成
 * 已播放或已送达。所有回调均由同一个拥有者同步调用，回调中不得再次调用
 * runtime API，也不得保存 PCM 指针。
 */
typedef enum {
    PET_AI_PORT_OK,
    PET_AI_PORT_EMPTY,
    PET_AI_PORT_FAILED,
    PET_AI_PORT_INVALID,
} pet_ai_port_result_t;

typedef pet_ai_port_result_t (*pet_ai_runtime_void_fn)(void *context, uint32_t turn_id);
typedef pet_ai_port_result_t (*pet_ai_runtime_pcm_fn)(void *context, uint32_t turn_id,
                                                      const int16_t *pcm, size_t samples);

typedef struct {
    void *context;
    pet_ai_runtime_void_fn begin_turn;
    pet_ai_runtime_void_fn start_capture;
    pet_ai_runtime_void_fn stop_capture;
    pet_ai_runtime_pcm_fn forward_pcm;
    pet_ai_runtime_void_fn end_input;
    pet_ai_runtime_void_fn cancel_turn;
    pet_ai_runtime_void_fn start_playback;
    pet_ai_runtime_void_fn stop_playback;
    pet_ai_runtime_void_fn clear_audio;
} pet_ai_runtime_sink_t;

typedef pet_ai_port_result_t (*pet_ai_runtime_wake_fn)(void *context, const int16_t *pcm,
                                                       size_t samples, uint64_t now_ms,
                                                       bool *detected);
typedef pet_ai_port_result_t (*pet_ai_runtime_vad_fn)(void *context, const int16_t *pcm,
                                                      size_t samples, uint64_t now_ms,
                                                      pet_ai_activity_t *activity);

typedef struct {
    void *context;
    /* 可为空：缺少模型 VAD 时回退到 session 的 RMS 滞回。 */
    pet_ai_runtime_wake_fn detect_wake;
    pet_ai_runtime_vad_fn classify_vad;
    /* 可选生命周期由 agent 的音频拥有者调用；裸 runtime 不初始化外部资源。
     * open 失败须自行回滚；成功后 close 只调用一次。reset 丢弃残留 PCM/模型状态。
     * 不启动额外任务、不保存外部 PCM 指针，也不得重入 agent/runtime API。 */
    pet_ai_port_result_t (*open)(void *context);
    pet_ai_port_result_t (*reset)(void *context);
    pet_ai_port_result_t (*close)(void *context);
    /* 可选的 16 kHz/20 ms 前处理；输出同时用于唤醒、VAD 与上传，
     * 只能处理已通过状态/时间预检的 PCM。不得保存输入/输出指针。 */
    pet_ai_port_result_t (*preprocess)(void *context,const int16_t *in,size_t samples,
        uint64_t now_ms,int16_t *out);
} pet_ai_runtime_frontend_t;

typedef enum {
    PET_AI_RUNTIME_OK,
    PET_AI_RUNTIME_IGNORED,
    PET_AI_RUNTIME_STALE,
    PET_AI_RUNTIME_INVALID,
    PET_AI_RUNTIME_NOT_READY,
    PET_AI_RUNTIME_PORT_FAILED,
    PET_AI_RUNTIME_BUSY,
} pet_ai_runtime_result_t;

typedef struct {
    pet_ai_step_t session_step;
    pet_ai_result_t session_result;
    pet_ai_port_result_t port_result;
    /* 清理只尝试一次；失败时拥有者需要执行硬件级恢复。 */
    pet_ai_port_result_t cleanup_result;
} pet_ai_runtime_step_t;

/* 调用方持有实例；不要跨任务共享，所有 API 必须由同一运行任务串行调用。 */
typedef struct {
    pet_ai_session_t session;
    pet_ai_runtime_sink_t sink;
    pet_ai_runtime_frontend_t frontend;
    bool ready;
    bool applying;
} pet_ai_runtime_t;

pet_ai_runtime_result_t pet_ai_runtime_init(pet_ai_runtime_t *runtime,
                                            const pet_ai_session_config_t *session_config,
                                            const pet_ai_runtime_sink_t *sink,
                                            const pet_ai_runtime_frontend_t *frontend,
                                            uint64_t now_ms);

bool pet_ai_runtime_snapshot(const pet_ai_runtime_t *runtime, pet_ai_view_t *view);

/* 外部事件（按键、云端响应、播放 DMA drain、取消/静音）通过此入口串行注入。 */
pet_ai_runtime_result_t pet_ai_runtime_handle(pet_ai_runtime_t *runtime,
                                               const pet_ai_input_t *input,
                                               pet_ai_runtime_step_t *step);

/* 运行任务读取到一个 20 ms PCM 帧后调用；唤醒/VAD 适配器在此边界运行。 */
pet_ai_runtime_result_t pet_ai_runtime_frame(pet_ai_runtime_t *runtime,
                                              const int16_t *pcm, size_t samples,
                                              uint64_t now_ms,
                                              pet_ai_runtime_step_t *step);
