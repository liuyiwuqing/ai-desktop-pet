#include "pet/hal/audio_buffer.h"

#include <string.h>

#include "pet/audio.h"

bool pet_audio_buffer_init(pet_audio_buffer_t *buffer, int16_t *storage, size_t capacity)
{
    if (buffer == NULL || storage == NULL || capacity == 0 || capacity > PET_AUDIO_BUFFER_SAMPLES)
        return false;
    *buffer = (pet_audio_buffer_t){.samples = storage, .capacity = capacity};
    pet_audio_buffer_clear(buffer);
    return true;
}

bool pet_audio_buffer_push(pet_audio_buffer_t *buffer, const int16_t *pcm, size_t samples)
{
    if (buffer == NULL || buffer->samples == NULL || pcm == NULL ||
        samples > buffer->capacity - buffer->count) return false;
    const size_t tail = (buffer->head + buffer->count) % buffer->capacity;
    const size_t first = samples < buffer->capacity - tail ? samples : buffer->capacity - tail;
    memcpy(buffer->samples + tail, pcm, first * sizeof(*pcm));
    memcpy(buffer->samples, pcm + first, (samples - first) * sizeof(*pcm));
    buffer->count += samples;
    return true;
}

size_t pet_audio_buffer_read(pet_audio_buffer_t *buffer, int16_t *pcm, size_t capacity)
{
    if (buffer == NULL || buffer->samples == NULL || pcm == NULL) return 0;
    const size_t count = capacity < buffer->count ? capacity : buffer->count;
    const size_t first = count < buffer->capacity - buffer->head ? count : buffer->capacity - buffer->head;
    memcpy(pcm, buffer->samples + buffer->head, first * sizeof(*pcm));
    memcpy(pcm + first, buffer->samples, (count - first) * sizeof(*pcm));
    memset(buffer->samples + buffer->head, 0, first * sizeof(*pcm));
    memset(buffer->samples, 0, (count - first) * sizeof(*pcm));
    buffer->head = (buffer->head + count) % buffer->capacity;
    buffer->count -= count;
    return count;
}

void pet_audio_buffer_clear(pet_audio_buffer_t *buffer)
{
    if (buffer == NULL || buffer->samples == NULL) return;
    memset(buffer->samples, 0, buffer->capacity * sizeof(*buffer->samples));
    buffer->head = 0;
    buffer->count = 0;
}
