#include <assert.h>
#include <limits.h>
#include <stdio.h>

#include "pet/hal/audio_pcm.h"

int main(void)
{
    assert(pet_audio_pcm_from_i2s(0) == 0);
    assert(pet_audio_pcm_from_i2s(INT32_MAX - 255) == INT16_MAX);
    assert(pet_audio_pcm_from_i2s(INT32_MIN) == INT16_MIN);
    assert(pet_audio_pcm_from_i2s(0x12345600) == 0x1234);
    assert(pet_audio_pcm_from_i2s(-65536) == -1);
    assert(pet_audio_pcm_from_i2s(-256) == -1);

    const int16_t input[] = {INT16_MIN, -1000, 0, 1000, INT16_MAX};
    int16_t output[5];
    assert(pet_audio_pcm_volume(input, output, 5, 50));
    assert(output[0] == -16384 && output[1] == -500 && output[2] == 0);
    assert(output[3] == 500 && output[4] == 16383);
    assert(pet_audio_pcm_volume(input, output, 5, 100));
    assert(output[0] == INT16_MIN && output[4] == INT16_MAX);
    assert(pet_audio_pcm_volume(output, output, 5, 0));
    for (unsigned i = 0; i < 5; ++i) assert(output[i] == 0);
    output[0] = 123;
    assert(!pet_audio_pcm_volume(input, output, 5, 101));
    assert(output[0] == 123);
    assert(!pet_audio_pcm_volume(NULL, output, 5, 50));

    pet_audio_level_t level;
    const int16_t quiet[] = {0, 0, 0, 0};
    assert(pet_audio_pcm_measure(quiet, 4, &level));
    assert(level.rms == 0 && level.peak == 0 && level.level_pct == 0);
    const int16_t full[] = {INT16_MIN, INT16_MIN};
    assert(pet_audio_pcm_measure(full, 2, &level));
    assert(level.rms == 32768 && level.peak == 32768 && level.level_pct == 100);
    const int16_t known[] = {3000, 4000};
    assert(pet_audio_pcm_measure(known, 2, &level));
    assert(level.rms == 3535 && level.peak == 4000);
    const int16_t minus_30_db[] = {1036, -1036};
    assert(pet_audio_pcm_measure(minus_30_db, 2, &level));
    assert(level.level_pct >= 49 && level.level_pct <= 50);
    assert(!pet_audio_pcm_measure(input, 0, &level));
    assert(!pet_audio_pcm_measure(NULL, 1, &level));
    puts("audio PCM: alignment, bounded volume and RMS/dBFS passed");
}
