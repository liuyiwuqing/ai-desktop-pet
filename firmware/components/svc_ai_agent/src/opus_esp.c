#include "svc_ai_agent/opus.h"

#include <string.h>

#include "esp_opus_enc.h"
#include "esp_opus_dec.h"

static esp_err_t sdk_error(esp_audio_err_t error)
{
    if (error == ESP_AUDIO_ERR_MEM_LACK) return ESP_ERR_NO_MEM;
    if (error == ESP_AUDIO_ERR_INVALID_PARAMETER) return ESP_ERR_INVALID_ARG;
    return ESP_FAIL;
}

esp_err_t pet_opus_open(pet_opus_t *codec)
{
    if (codec == NULL) return ESP_ERR_INVALID_ARG;
    if (codec->encoder != NULL || codec->decoder != NULL || codec->ready || codec->release_failed)
        return ESP_ERR_INVALID_STATE;
    esp_opus_enc_config_t encoder_config = ESP_OPUS_ENC_CONFIG_DEFAULT();
    encoder_config.sample_rate = PET_AUDIO_CAPTURE_RATE;
    encoder_config.channel = 1;
    encoder_config.bits_per_sample = 16;
    encoder_config.bitrate = 16000;
    encoder_config.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    encoder_config.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    encoder_config.complexity = 0;
    encoder_config.enable_vbr = true;
    void *encoder = NULL, *decoder = NULL;
    esp_audio_err_t error = esp_opus_enc_open(&encoder_config, sizeof(encoder_config), &encoder);
    if (error != ESP_AUDIO_ERR_OK || encoder == NULL) {
        if (encoder != NULL) esp_opus_enc_close(encoder);
        return sdk_error(error);
    }
    int input_bytes = 0, output_bytes = 0;
    error = esp_opus_enc_get_frame_size(encoder, &input_bytes, &output_bytes);
    if (error != ESP_AUDIO_ERR_OK || input_bytes != (int)(PET_XZ_ENCODE_SAMPLES * sizeof(int16_t)) ||
        output_bytes <= 0 || output_bytes > (int)PET_XZ_PACKET_BYTES) {
        esp_opus_enc_close(encoder);
        return error == ESP_AUDIO_ERR_OK ? ESP_ERR_INVALID_SIZE : sdk_error(error);
    }
    esp_opus_dec_cfg_t decoder_config = {
        .sample_rate = PET_AUDIO_PLAYBACK_RATE, .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS, .self_delimited = false,
    };
    error = esp_opus_dec_open(&decoder_config, sizeof(decoder_config), &decoder);
    if (error != ESP_AUDIO_ERR_OK || decoder == NULL) {
        if (decoder != NULL && esp_opus_dec_close(decoder) != ESP_AUDIO_ERR_OK)
            codec->release_failed = true;
        esp_opus_enc_close(encoder);
        return sdk_error(error);
    }
    codec->encoder = encoder;
    codec->decoder = decoder;
    codec->ready = true;
    return ESP_OK;
}

esp_err_t pet_opus_close(pet_opus_t *codec)
{
    if (codec == NULL) return ESP_ERR_INVALID_ARG;
    if (codec->release_failed) return ESP_FAIL;
    if (codec->encoder != NULL) esp_opus_enc_close(codec->encoder);
    if (codec->decoder != NULL && esp_opus_dec_close(codec->decoder) != ESP_AUDIO_ERR_OK)
        codec->release_failed = true;
    codec->encoder = codec->decoder = NULL;
    codec->ready = false;
    return codec->release_failed ? ESP_FAIL : ESP_OK;
}

static bool reset(void *context)
{
    pet_opus_t *codec = context;
    if (codec == NULL || !codec->ready) return false;
    const esp_audio_err_t encoded = esp_opus_enc_reset(codec->encoder);
    const esp_audio_err_t decoded = esp_opus_dec_reset(codec->decoder);
    if (encoded != ESP_AUDIO_ERR_OK || decoded != ESP_AUDIO_ERR_OK) codec->ready = false;
    return codec->ready;
}

static bool encode(void *context, const int16_t *pcm, uint8_t *out, size_t capacity, size_t *bytes)
{
    if (bytes != NULL) *bytes = 0;
    pet_opus_t *codec = context;
    if (codec == NULL || !codec->ready || pcm == NULL || out == NULL || bytes == NULL ||
        capacity < PET_XZ_PACKET_BYTES || capacity > UINT32_MAX) return false;
    esp_audio_enc_in_frame_t input = {
        .buffer = (uint8_t *)pcm, .len = PET_XZ_ENCODE_SAMPLES * sizeof(int16_t),
    };
    esp_audio_enc_out_frame_t output = {.buffer = out, .len = PET_XZ_PACKET_BYTES};
    if (esp_opus_enc_process(codec->encoder, &input, &output) != ESP_AUDIO_ERR_OK ||
        output.encoded_bytes == 0 || output.encoded_bytes > PET_XZ_PACKET_BYTES) {
        codec->ready = false;
        return false;
    }
    *bytes = output.encoded_bytes;
    return true;
}

static bool decode(void *context, const uint8_t *packet, size_t bytes, int16_t *out,
                    size_t capacity, size_t *samples)
{
    if (samples != NULL) *samples = 0;
    pet_opus_t *codec = context;
    if (codec == NULL || !codec->ready || packet == NULL || bytes == 0 ||
        bytes > PET_XZ_PACKET_BYTES || out == NULL || capacity != PET_XZ_DECODE_SAMPLES ||
        samples == NULL) return false;
    esp_audio_dec_in_raw_t input = {.buffer = (uint8_t *)packet, .len = (uint32_t)bytes};
    esp_audio_dec_out_frame_t output = {.buffer = (uint8_t *)out, .len = capacity * sizeof(int16_t)};
    esp_audio_dec_info_t info = {0};
    if (esp_opus_dec_decode(codec->decoder, &input, &output, &info) != ESP_AUDIO_ERR_OK ||
        input.consumed != bytes || output.decoded_size != PET_XZ_DECODE_SAMPLES * sizeof(int16_t) ||
        info.sample_rate != PET_AUDIO_PLAYBACK_RATE || info.channel != 1 || info.bits_per_sample != 16) {
        memset(out, 0, capacity * sizeof(int16_t));
        codec->ready = false;
        return false;
    }
    *samples = PET_XZ_DECODE_SAMPLES;
    return true;
}

pet_xz_codec_t pet_opus_port(pet_opus_t *codec)
{
    return (pet_xz_codec_t){.context = codec, .reset = reset, .encode = encode, .decode = decode};
}
