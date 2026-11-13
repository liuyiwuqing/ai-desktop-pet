#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "pet/audio.h"

/* INMP441 的 24-bit 数据左对齐于 32-bit 槽；取高 16 位，不额外放大。 */
int16_t pet_audio_pcm_from_i2s(int32_t raw);

/* 仅做 0..100% 衰减；支持原位，非法参数时不修改输出。 */
bool pet_audio_pcm_volume(const int16_t *input, int16_t *output,
                          size_t samples, uint8_t volume_pct);
bool pet_audio_pcm_measure(const int16_t *pcm, size_t samples, pet_audio_level_t *out);
