#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "svc_ai_agent/opus.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"

/* 真实 SDK 头文件 + 外部二进制库边界替身，不用替身声称验证了 Opus 音质。 */
static int enc_token, dec_token;
static unsigned enc_opened, dec_opened, enc_closed, dec_closed;
static bool enc_fail, dec_fail, size_fail, reset_fail, process_fail, close_fail;
static bool bad_consumed, bad_format, bad_output;

esp_audio_err_t esp_opus_enc_open(void *cfg, uint32_t bytes, void **handle)
{
    const esp_opus_enc_config_t *config = cfg;
    assert(bytes == sizeof(*config) && config->sample_rate == 16000 && config->channel == 1);
    assert(config->bits_per_sample == 16 && config->frame_duration == ESP_OPUS_ENC_FRAME_DURATION_60_MS);
    assert(config->application_mode == ESP_OPUS_ENC_APPLICATION_VOIP && config->bitrate == 16000);
    assert(!config->enable_fec && !config->enable_dtx);
    ++enc_opened; *handle = enc_fail ? NULL : &enc_token;
    return enc_fail ? ESP_AUDIO_ERR_MEM_LACK : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_get_frame_size(void *handle, int *input, int *output)
{
    assert(handle == &enc_token); *input = size_fail ? 640 : 1920; *output = 1275;
    return ESP_AUDIO_ERR_OK;
}
void esp_opus_enc_close(void *handle) { assert(handle == &enc_token); ++enc_closed; }
esp_audio_err_t esp_opus_dec_open(void *cfg, uint32_t bytes, void **handle)
{
    const esp_opus_dec_cfg_t *config = cfg;
    assert(bytes == sizeof(*config) && config->sample_rate == 24000 && config->channel == 1);
    assert(config->frame_duration == ESP_OPUS_DEC_FRAME_DURATION_60_MS && !config->self_delimited);
    ++dec_opened; *handle = dec_fail ? NULL : &dec_token;
    return dec_fail ? ESP_AUDIO_ERR_MEM_LACK : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_close(void *handle)
{
    assert(handle == &dec_token); ++dec_closed;
    return close_fail ? ESP_AUDIO_ERR_FAIL : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_reset(void *handle)
{
    assert(handle == &enc_token); return reset_fail ? ESP_AUDIO_ERR_FAIL : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_reset(void *handle)
{
    assert(handle == &dec_token); return reset_fail ? ESP_AUDIO_ERR_FAIL : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_process(void *handle, esp_audio_enc_in_frame_t *in,
                                    esp_audio_enc_out_frame_t *out)
{
    assert(handle == &enc_token && in->len == 1920 && out->len >= 1500);
    memcpy(out->buffer, "\x18\x11\x22", 3); out->encoded_bytes = bad_output ? out->len + 1 : 3;
    return process_fail ? ESP_AUDIO_ERR_FAIL : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_decode(void *handle, esp_audio_dec_in_raw_t *raw,
                                   esp_audio_dec_out_frame_t *frame, esp_audio_dec_info_t *info)
{
    assert(handle == &dec_token && raw->len == 3 && frame->len == 2880);
    raw->consumed = bad_consumed ? 2 : raw->len;
    frame->decoded_size = bad_output ? 3000 : 2880;
    info->sample_rate = bad_format ? 16000 : 24000;
    info->channel = 1; info->bits_per_sample = 16;
    memset(frame->buffer, 0, frame->len);
    return process_fail ? ESP_AUDIO_ERR_FAIL : ESP_AUDIO_ERR_OK;
}

int main(void)
{
    pet_opus_t codec = {0};
    enc_fail = true;
    assert(pet_opus_open(&codec) == ESP_ERR_NO_MEM && enc_closed == 0);
    enc_fail = false; dec_fail = true;
    assert(pet_opus_open(&codec) == ESP_ERR_NO_MEM && enc_closed == 1);
    dec_fail = false; size_fail = true;
    assert(pet_opus_open(&codec) != ESP_OK && enc_closed == 2);
    size_fail = false;
    assert(pet_opus_open(&codec) == ESP_OK);
    assert(pet_opus_open(&codec) == ESP_ERR_INVALID_STATE);
    pet_xz_codec_t port = pet_opus_port(&codec);
    assert(port.reset(port.context));
    int16_t pcm[PET_XZ_ENCODE_SAMPLES] = {0}, playback[PET_XZ_DECODE_SAMPLES];
    uint8_t packet[PET_XZ_PACKET_BYTES]; size_t bytes, samples;
    assert(port.encode(port.context, pcm, packet, sizeof(packet), &bytes) && bytes == 3);
    assert(port.decode(port.context, packet, bytes, playback, PET_XZ_DECODE_SAMPLES, &samples));
    assert(samples == 1440);
    assert(!port.encode(port.context, pcm, packet, 10, &bytes) && bytes == 0);
    assert(!port.decode(port.context, packet, 3, playback, 20, &samples) && samples == 0);
    assert(pet_opus_close(&codec) == ESP_OK);
    assert(pet_opus_close(&codec) == ESP_OK);
    for (unsigned scenario = 0; scenario < 6; ++scenario) {
        assert(pet_opus_open(&codec) == ESP_OK);
        port = pet_opus_port(&codec);
        reset_fail = scenario == 0; process_fail = scenario == 1;
        bad_consumed = scenario == 2; bad_format = scenario == 3; bad_output = scenario >= 4;
        if (scenario == 0) assert(!port.reset(port.context));
        else if (scenario == 1 || scenario == 4)
            assert(!port.encode(port.context, pcm, packet, sizeof(packet), &bytes) && bytes == 0);
        else assert(!port.decode(port.context, packet, 3, playback, PET_XZ_DECODE_SAMPLES, &samples) && samples == 0);
        assert(!port.reset(port.context)); /* 已损坏的 codec 不能继续消费 PCM。 */
        assert(pet_opus_close(&codec) == ESP_OK);
        reset_fail = process_fail = bad_consumed = bad_format = bad_output = false;
    }
    assert(pet_opus_open(&codec) == ESP_OK);
    close_fail = true;
    assert(pet_opus_close(&codec) == ESP_FAIL);
    const unsigned closed = dec_closed;
    assert(pet_opus_close(&codec) == ESP_FAIL && dec_closed == closed);
    assert(pet_opus_open(&codec) == ESP_ERR_INVALID_STATE);
    assert(enc_opened == enc_closed + 1); /* 唯一未分配成功的 encoder 无需释放。 */
    assert(dec_opened == dec_closed + 1);
    puts("Opus port: fixed SDK configuration, cleanup, frame sizes and fail-closed decoding passed");
}
