#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pet/audio.h"

/*
 * 单一任务串行拥有整个设备；禁止 ISR 调用及跨任务并发调用。
 * init 不开麦、不放音。capture/playback 互斥；stop/deinit 可重复清理。
 * read/write 返回实际样本数，错误时也可能已有部分进度；返回后不引用调用方缓冲区。
 * write 只表示送入 DMA，播放完成必须另行 drain。失败后先 deinit 再重试；
 * deinit 失败不得报告已静音。
 */
typedef struct {
    const char *id;
    esp_err_t (*init)(void);
    esp_err_t (*deinit)(void);
    esp_err_t (*capture_start)(void);
    esp_err_t (*capture_read)(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout_ms);
    esp_err_t (*capture_stop)(void);
    esp_err_t (*playback_start)(uint32_t sample_rate);
    esp_err_t (*playback_write)(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout_ms);
    esp_err_t (*playback_drain)(uint32_t timeout_ms);
    esp_err_t (*playback_stop)(void);
} pet_audio_device_t;

/* RX 固定 16 kHz；TX 支持 16 kHz 诊断回放与 24 kHz TTS，不隐式重采样。 */
const pet_audio_device_t *pet_bsp_audio(void);

/* 仅 fake 配置/主机测试使用，不代表实物录音或播放。 */
const pet_audio_device_t *pet_fake_audio(void);
