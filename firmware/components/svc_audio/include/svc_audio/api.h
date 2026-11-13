#pragma once

#include <stdbool.h>

#include "pet/hal/audio.h"

#define PET_AUDIO_ERR_CANCELLED 0x7801

typedef enum {
    PET_AUDIO_CHECK_INIT,
    PET_AUDIO_CHECK_CAPTURE,
    PET_AUDIO_CHECK_PLAYBACK,
    PET_AUDIO_CHECK_DRAIN,
    PET_AUDIO_CHECK_DONE,
    PET_AUDIO_CHECK_DUPLEX,
} pet_audio_check_phase_t;

typedef struct {
    uint16_t duration_ms; /* 20..1000，20 ms 的整数倍。 */
    uint8_t volume_pct;   /* 0..100，仅衰减。 */
    int16_t *storage;     /* 调用方提供；不与其他缓冲重叠；返回前清除录音。 */
    size_t storage_samples;
    bool (*cancelled)(void *ctx); /* 可选；在帧边界检查，不允许在回调中调用 HAL。 */
    void *ctx;
} pet_audio_loopback_config_t;

typedef struct {
    pet_audio_check_phase_t phase;
    uint32_t recorded_samples;
    uint32_t submitted_samples; /* 送入 TX 的样本数，不代表已经发声。 */
    pet_audio_level_t capture_level;
    bool playback_drained;
    esp_err_t cleanup_error;
} pet_audio_loopback_report_t;

/* 独占并负责 HAL 生命周期；先完整录音，再以原 16 kHz 回放，不连接网络/不写存档。 */
esp_err_t pet_audio_loopback_run(const pet_audio_device_t *device,
                                 const pet_audio_loopback_config_t *config,
                                 pet_audio_loopback_report_t *report);

typedef struct {
    uint16_t duration_ms; /* 20..1000，20 ms 整帧；采集阶段另有 250 ms 总余量。 */
    uint8_t volume_pct;   /* 0..25，仅输出低幅合成 400 Hz 提示音，不回放麦克风。 */
    bool (*cancelled)(void *ctx);
    void *ctx;
} pet_audio_duplex_config_t;

typedef struct {
    pet_audio_check_phase_t phase;
    uint32_t recorded_samples, submitted_samples;
    uint16_t capture_peak;
    uint64_t elapsed_us, max_capture_gap_us; /* 软件 I/O 观测，不是 DMA/声学时间戳。 */
    bool duplex_started, playback_drained;
    esp_err_t cleanup_error;
} pet_audio_duplex_report_t;

/* 显式独占 HAL 的有限并行诊断：RX 16 kHz/TX 24 kHz；无网络/存储/转写。
 * 未实现 duplex_start 的设备返回 NOT_SUPPORTED；不把合成 TX 当作校准过的 AEC 参考。 */
esp_err_t pet_audio_duplex_run(const pet_audio_device_t *device,
                              const pet_audio_duplex_config_t *config,
                              pet_audio_duplex_report_t *report);

/* 仅 CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT 启用时由组合根串行调用。
 * allowed 必需，启动及帧边界重新检查；ctx 须持续有效，回调不得重入 HAL。 */
esp_err_t pet_audio_diagnostic_start(bool (*allowed)(void *ctx), void *ctx);
