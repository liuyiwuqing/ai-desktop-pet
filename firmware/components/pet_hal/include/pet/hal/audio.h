#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "pet/audio.h"

/*
 * 单一任务串行拥有整个设备；禁止 ISR 调用及跨任务并发调用。
 * init 不开麦、不放音。原 capture_start/playback_start 保持互斥；
 * 可选 duplex_start 显式一次启动 RX/TX，不改变原入口策略。stop/deinit 可重复清理。
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
    /* 可为空（不支持）。仅从两个方向均停止时启动；RX 仍为 16 kHz。
     * 支持并行 I/O 不代表时钟同步、AEC 参考对齐或声学打断已校准。
     * 失败后仍须 deinit；回滚失败不能当作已经关麦/静音。 */
    esp_err_t (*duplex_start)(uint32_t playback_sample_rate);
} pet_audio_device_t;

/* RX 固定 16 kHz；TX 支持 16 kHz 诊断回放与 24 kHz TTS，不隐式重采样。 */
const pet_audio_device_t *pet_bsp_audio(void);

/* 仅 fake 配置/主机测试使用，不代表实物录音或播放。 */
const pet_audio_device_t *pet_fake_audio(void);
