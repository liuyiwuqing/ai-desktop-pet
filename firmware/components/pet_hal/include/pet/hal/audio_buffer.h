#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 调用方提供 PCM 存储；单一拥有者使用，不在 ISR/多任务间裸共享。 */
typedef struct {
    int16_t *samples;
    size_t capacity;
    size_t head;
    size_t count;
} pet_audio_buffer_t;

bool pet_audio_buffer_init(pet_audio_buffer_t *buffer, int16_t *storage, size_t capacity);
/* 空间不足则整段拒绝，不丢弃尚未消费的声音。 */
bool pet_audio_buffer_push(pet_audio_buffer_t *buffer, const int16_t *pcm, size_t samples);
size_t pet_audio_buffer_read(pet_audio_buffer_t *buffer, int16_t *pcm, size_t capacity);
/* 清除索引及存储内容，避免下一会话读到旧录音。 */
void pet_audio_buffer_clear(pet_audio_buffer_t *buffer);
