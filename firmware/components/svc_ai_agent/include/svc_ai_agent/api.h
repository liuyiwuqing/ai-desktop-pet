#pragma once

#include "esp_err.h"
#include "pet/hal/audio.h"
#include "svc_ai_agent/runtime.h"
#include "svc_ai_agent/context.h"
#include "pet/message.h"

typedef struct {
    void *context;
    pet_ai_runtime_void_fn begin_turn;
    pet_ai_runtime_pcm_fn forward_pcm;
    pet_ai_runtime_void_fn end_input;
    pet_ai_runtime_void_fn cancel_turn;
} pet_ai_transport_t;

typedef struct {
    bool muted; /* 用户静音，与 DND 独立。 */
    bool dnd; /* 禁止采集和播放，显式 PTT 也不能越过。 */
    bool interrupted; /* critical 画面持有期间取消会话并禁止唤醒/PTT。 */
} pet_ai_policy_t;

typedef struct {
    const pet_audio_device_t *audio;
    pet_ai_transport_t transport;
    pet_ai_runtime_frontend_t frontend;
    uint8_t volume_pct;
    pet_ai_policy_t initial_policy;
    bool notifications_only; /* 无传输、无前端、不开麦；只拥有提示音输出。 */
    void (*on_completed)(uint32_t turn_id); /* 音频拥有者仅发送有界完成通知，不得阻塞。 */
} pet_ai_agent_config_t;

typedef struct {
    pet_ai_view_t view;
    bool voice_enabled;
    bool ready; /* 任务/HAL/已配置的前端已就绪；单轮会话仍可能处于 ERROR。 */
    bool faulted; /* 已锁住，需重启；不等于物理已静音。 */
    esp_err_t last_error; /* 最近端口/HAL 失败；会话超时等原因见 view.reason。 */
    esp_err_t cleanup_error;
    uint32_t rejected_commands;
    uint32_t stale_items;
    uint32_t captured_frames;
    uint64_t submitted_samples;
    uint8_t volume_pct;
    uint8_t playback_level_pct;
    uint32_t playback_underruns;
    uint16_t buffered_samples;
    uint32_t inference_count,inference_last_ms,inference_max_ms,owner_stack_free;
    char transcript[PET_AI_TEXT_MAX+1], reply[PET_AI_TEXT_MAX+1];
    pet_ai_policy_t policy; /* 请求值；不等于硬件已完成。 */
    bool policy_pending;
    bool ptt_requested;
    bool capture_active, playback_active; /* 最近一次成功 HAL 启停；faulted 时不保证物理状态。 */
} pet_ai_agent_snapshot_t;

/*
 * 仅由组合根启动一次，不与音频诊断或其他 HAL 拥有者同时使用。
 * ESP_OK 表示任务已创建；异步硬件初始化结果看 snapshot.ready/faulted。
 * 配置按值复制，audio 和回调 context 必须覆盖运行期；回调不等待网络、不重入。
 * 模型推理同步执行，其耗时、8 KB 拥有者栈与堆水位需上板核验。
 * 服务资源持续到重启；HAL/清理失败锁住服务，不自动重新初始化或回绕轮次。
 * 无默认传输、无开机自动启动。所有 HAL 调用（含 init/deinit）只在拥有者任务。
 */
esp_err_t pet_ai_agent_start(const pet_ai_agent_config_t *config);

/*
 * 非 ISR、可跨任务、非阻塞。普通控制只保证已入队；队满返回 ESP_ERR_TIMEOUT。
 * MUTE/UNMUTE/CANCEL 使用优先邮箱，队满也接受，并作废更早的排队控制。
 * 时钟由消费任务生成；作用域同 session 输入。tick/drained 仅由拥有者生成。
 * turn_id 必须来自此服务快照，不能在服务之间或重启后复用。
 */
esp_err_t pet_ai_agent_post(pet_ai_input_kind_t kind, uint32_t turn_id);

/* 单一 UI 输入源的按住电平；拥有者绑定轮次并处理松开，无需调用者猜测 turn_id。
 * 同一调度周期内的快速按下/松开可合并为未开始；取消/策略变化后需重新按下。
 * 不经普通队列。隐私禁止/未 ready 时按下返回 INVALID_STATE，松开仍可接受。
 */
esp_err_t pet_ai_agent_ptt(bool pressed);

/* 非 ISR、非阻塞、幂等；替换完整策略。由拥有者优先执行，不在调用任务操作 HAL。
 * 变化会取消当前会话、清除 PTT 并作废旧控制；解除后不恢复旧对话。
 * policy_pending=false 且无 fault/cleanup_error 才表示拥有者已处理请求；不是硬件断电开关。
 */
esp_err_t pet_ai_agent_set_policy(const pet_ai_policy_t *policy);

/* 只修改指定开关，原子保留其他策略；UI 中断与配置/DND 来源可独立更新。
 * 用户静音的单项更新使用 post(MUTE/UNMUTE, 0)，约束同完整策略接口。
 */
esp_err_t pet_ai_agent_set_dnd(bool enabled);
esp_err_t pet_ai_agent_set_interrupted(bool enabled);
esp_err_t pet_ai_agent_set_volume(uint8_t volume_pct); /* 新的 PCM 块生效，非阻塞。 */

/* 24 kHz mono s16，1..480 样本，返回前复制；finish=true 仅允许 samples=0。
 * 单一传输生产者按原顺序提交数据和结束标记；ESP_OK 不代表 DMA 已播放。
 * response_begin 与完整输入结束后才能播放；旧轮次在消费时丢弃。
 */
esp_err_t pet_ai_agent_play(uint32_t turn_id, const int16_t *pcm, size_t samples, bool finish);

/* 可跨任务复制快照；没有保存录音或传输文本。 */
esp_err_t pet_ai_agent_snapshot(pet_ai_agent_snapshot_t *snapshot);

/* 本轮 UTF-8 显示摘要，仅驻留 RAM；旧轮/隐私策略拒绝，取消/下一轮清零。 */
esp_err_t pet_ai_agent_text(uint32_t turn_id, pet_ai_text_role_t role,const char *text);

/* 上下文只在 RAM；静音/DND/中断、5 分钟空闲和显式清除都会清空。 */
esp_err_t pet_ai_agent_context(pet_ai_context_t *out);
esp_err_t pet_ai_agent_forget(void);

/* 提示音与语音共用音频拥有者；普通提示最多等三秒，静音/DND 始终拒绝。 */
esp_err_t pet_ai_agent_notify(pet_message_sound_t sound,bool critical);
