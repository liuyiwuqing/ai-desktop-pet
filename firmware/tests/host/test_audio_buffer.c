#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pet/hal/audio_buffer.h"

int main(void)
{
    pet_audio_buffer_t queue;
    int16_t storage[5] = {9, 9, 9, 9, 9};
    int16_t out[8] = {0};
    assert(pet_audio_buffer_init(&queue, storage, 5));
    const int16_t first[] = {1, 2, 3, 4};
    assert(pet_audio_buffer_push(&queue, first, 4));
    assert(!pet_audio_buffer_push(&queue, first, 2));
    assert(pet_audio_buffer_read(&queue, out, 3) == 3);
    assert(out[0] == 1 && out[1] == 2 && out[2] == 3);
    const int16_t wrapped[] = {5, 6, 7, 8};
    assert(pet_audio_buffer_push(&queue, wrapped, 4));
    assert(!pet_audio_buffer_push(&queue, first, 1));
    assert(pet_audio_buffer_read(&queue, out, 8) == 5);
    const int16_t expected[] = {4, 5, 6, 7, 8};
    assert(memcmp(out, expected, sizeof(expected)) == 0);
    assert(pet_audio_buffer_read(&queue, out, 8) == 0);
    assert(pet_audio_buffer_push(&queue, first, 4));
    pet_audio_buffer_clear(&queue);
    assert(pet_audio_buffer_read(&queue, out, 8) == 0);
    for (size_t i = 0; i < 5; ++i) assert(storage[i] == 0);
    assert(!pet_audio_buffer_init(&queue, NULL, 5));
    assert(!pet_audio_buffer_init(&queue, storage, 0));
    puts("audio buffer: bounded FIFO, wrap, overflow and erasure passed");
}
