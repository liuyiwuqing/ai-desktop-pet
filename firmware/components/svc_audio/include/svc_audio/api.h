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

/* 仅 CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT 启用时由组合根调用。 */
esp_err_t pet_audio_diagnostic_start(void);
