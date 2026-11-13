#pragma once

#include <stdint.h>

/* 业务边界统一为 signed 16-bit mono PCM；I2S 槽格式只在 BSP 内转换。 */
#define PET_AUDIO_CAPTURE_RATE 16000u
#define PET_AUDIO_PLAYBACK_RATE 24000u
#define PET_AUDIO_FRAME_MS 20u
#define PET_AUDIO_CAPTURE_SAMPLES 320u
#define PET_AUDIO_PLAYBACK_SAMPLES 480u
#define PET_AUDIO_BUFFER_SAMPLES 16000u

typedef struct {
    uint16_t rms;       /* 0..32768，保留 INT16_MIN 的绝对值。 */
    uint16_t peak;      /* 0..32768 */
    uint8_t level_pct;  /* -60 dBFS..0 dBFS 映射到 0..100 */
} pet_audio_level_t;
