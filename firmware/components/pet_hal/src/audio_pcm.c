#include "pet/hal/audio_pcm.h"

#include <limits.h>
#include <math.h>

int16_t pet_audio_pcm_from_i2s(int32_t raw)
{
    int32_t upper = (int32_t)((uint32_t)raw >> 16);
    if (upper > INT16_MAX) upper -= 65536;
    return (int16_t)upper;
}

bool pet_audio_pcm_volume(const int16_t *input, int16_t *output,
                          size_t samples, uint8_t volume_pct)
{
    if (input == NULL || output == NULL || volume_pct > 100) return false;
    for (size_t i = 0; i < samples; ++i)
        output[i] = (int16_t)((int32_t)input[i] * volume_pct / 100);
    return true;
}

bool pet_audio_pcm_measure(const int16_t *pcm, size_t samples, pet_audio_level_t *out)
{
    if (pcm == NULL || out == NULL || samples == 0 || samples > PET_AUDIO_BUFFER_SAMPLES)
        return false;
    uint64_t energy = 0;
    uint16_t peak = 0;
    for (size_t i = 0; i < samples; ++i) {
        const int32_t sample = pcm[i];
        const uint16_t magnitude = (uint16_t)(sample < 0 ? -sample : sample);
        if (magnitude > peak) peak = magnitude;
        energy += (uint64_t)((int64_t)sample * sample);
    }
    const double rms = sqrt((double)energy / (double)samples);
    double level = rms > 0 ? (20.0 * log10(rms / 32768.0) + 60.0) / 60.0 : 0;
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    *out = (pet_audio_level_t){.rms = (uint16_t)rms, .peak = peak,
                             .level_pct = (uint8_t)(level * 100.0)};
    return true;
}
