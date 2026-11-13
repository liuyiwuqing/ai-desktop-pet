#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "svc_ai_agent/xiaozhi.h"

static const char hello[] = "{\"type\":\"hello\",\"transport\":\"websocket\","
    "\"session_id\":\"session-1\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":24000,"
    "\"channels\":1,\"frame_duration\":60}}";

typedef struct {
    int16_t captured[PET_XZ_ENCODE_SAMPLES];
    unsigned encoded, decoded, resets;
    size_t encode_bytes;
    bool reset_fail, encode_fail, decode_fail, bad_encode_size, bad_decode_size;
} codec_boundary_t;

static bool reset(void *context)
{
    codec_boundary_t *codec = context; ++codec->resets; return !codec->reset_fail;
}
static bool encode(void *context, const int16_t *pcm, uint8_t *out, size_t cap, size_t *bytes)
{
    codec_boundary_t *codec = context; ++codec->encoded;
    assert(cap >= PET_XZ_PACKET_BYTES);
    memcpy(codec->captured, pcm, sizeof(codec->captured));
    memcpy(out, "\x18\x11\x22", 3);
    *bytes = codec->bad_encode_size ? cap + 1 : codec->encode_bytes ? codec->encode_bytes : 3;
    return !codec->encode_fail;
}
static bool decode(void *context, const uint8_t *in, size_t bytes, int16_t *out,
                    size_t cap, size_t *samples)
{
    codec_boundary_t *codec = context; ++codec->decoded;
    assert(in != NULL && bytes > 0 && cap == PET_XZ_DECODE_SAMPLES);
    for (size_t i = 0; i < cap; ++i) out[i] = (int16_t)(i + 100);
    *samples = codec->bad_decode_size ? cap + 1 : PET_XZ_DECODE_SAMPLES;
    return !codec->decode_fail;
}

static pet_xz_t open_client(codec_boundary_t *boundary, uint32_t turn, uint32_t *epoch)
{
    const pet_xz_codec_t codec = {
        .context = boundary, .reset = reset, .encode = encode, .decode = decode,
    };
    pet_xz_t client;
    assert(pet_xz_init(&client, &codec));
    assert(pet_xz_begin(&client, turn, epoch) == PET_XZ_OK);
    return client;
}

static void start_input(pet_xz_t *client, uint32_t epoch)
{
    pet_xz_event_t event;
    assert(pet_xz_receive_text(client, epoch, hello, strlen(hello), &event) == PET_XZ_OK);
    char json[PET_XZ_CONTROL_BYTES]; size_t bytes;
    assert(pet_xz_start_input(client, epoch, json, sizeof(json), &bytes) == PET_XZ_OK);
}

static void handshake(void)
{
    codec_boundary_t boundary = {0}; uint32_t epoch = 0;
    pet_xz_t client = open_client(&boundary, 7, &epoch);
    assert(epoch == 1);
    assert(strstr(pet_xz_hello(), "\"frame_duration\":60") != NULL);
    pet_xz_event_t event;
    assert(pet_xz_receive_text(&client, epoch, hello, strlen(hello), &event) == PET_XZ_OK);
    assert(event.kind == PET_XZ_EVENT_HELLO && event.turn_id == 7 && event.epoch == epoch);
    char json[PET_XZ_CONTROL_BYTES]; size_t bytes = 0;
    assert(pet_xz_start_input(&client, epoch, json, sizeof(json), &bytes) == PET_XZ_OK);
    assert(bytes == strlen(json));
    assert(strcmp(json, "{\"session_id\":\"session-1\",\"type\":\"listen\","
                        "\"state\":\"start\",\"mode\":\"manual\"}") == 0);
}

static void capture_and_playback(void)
{
    codec_boundary_t boundary = {0}; uint32_t epoch;
    pet_xz_t client = open_client(&boundary, 8, &epoch);
    start_input(&client, epoch);
    uint8_t packet[PET_XZ_PACKET_BYTES]; size_t bytes;
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES];
    for (size_t frame = 0; frame < 4; ++frame) {
        for (size_t i = 0; i < PET_AUDIO_CAPTURE_SAMPLES; ++i) pcm[i] = (int16_t)(100 * (frame + 1));
        assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES,
                              packet, sizeof(packet), &bytes) == PET_XZ_OK);
        if (frame == 2) {
            assert(bytes == 3 && memcmp(packet, "\x18\x11\x22", 3) == 0);
            assert(boundary.captured[0] == 100 && boundary.captured[319] == 100);
            assert(boundary.captured[320] == 200 && boundary.captured[639] == 200);
            assert(boundary.captured[640] == 300 && boundary.captured[959] == 300);
        } else assert(bytes == 0);
    }
    char json[PET_XZ_CONTROL_BYTES]; size_t json_bytes;
    assert(pet_xz_end_input(&client, epoch, packet, sizeof(packet), &bytes,
                            json, sizeof(json), &json_bytes) == PET_XZ_OK);
    assert(bytes == 3 && boundary.encoded == 2);
    assert(boundary.captured[0] == 400 && boundary.captured[319] == 400);
    for (size_t i = 320; i < 960; ++i) assert(boundary.captured[i] == 0);
    assert(strcmp(json, "{\"session_id\":\"session-1\",\"type\":\"listen\",\"state\":\"stop\"}") == 0);
    assert(json_bytes == strlen(json));
    pet_xz_event_t event;
    const char start[] = "{\"type\":\"tts\",\"state\":\"start\"}";
    assert(pet_xz_receive_text(&client, epoch, start, strlen(start), &event) == PET_XZ_OK);
    assert(event.kind == PET_XZ_EVENT_RESPONSE_BEGIN && event.turn_id == 8);
    assert(pet_xz_receive_audio(&client, epoch, packet, bytes) == PET_XZ_OK);
    assert(pet_xz_receive_audio(&client, epoch, packet, bytes) == PET_XZ_BUSY);
    assert(boundary.decoded == 1);
    pet_xz_playback_t chunk, same;
    for (size_t i = 0; i < 3; ++i) {
        assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_OK);
        assert(!chunk.finish && chunk.samples == 480);
        assert(chunk.pcm[0] == (int16_t)(100 + 480 * i));
        assert(chunk.pcm[479] == (int16_t)(579 + 480 * i));
        assert(pet_xz_peek_playback(&client, epoch, &same) == PET_XZ_OK);
        assert(same.pcm == chunk.pcm && same.samples == chunk.samples);
        assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_OK);
    }
    assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_IGNORED);
    const char stop[] = "{\"type\":\"tts\",\"state\":\"stop\"}";
    assert(pet_xz_receive_text(&client, epoch, stop, strlen(stop), &event) == PET_XZ_OK);
    assert(event.kind == PET_XZ_EVENT_RESPONSE_END);
    assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_OK);
    assert(chunk.finish && chunk.samples == 0 && chunk.pcm == NULL);
    assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_OK);
    assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_IGNORED);
}

static pet_xz_t waiting(codec_boundary_t *boundary, uint32_t *epoch)
{
    pet_xz_t client = open_client(boundary, 10, epoch);
    start_input(&client, *epoch);
    uint8_t packet[PET_XZ_PACKET_BYTES]; char json[PET_XZ_CONTROL_BYTES]; size_t pb, jb;
    assert(pet_xz_end_input(&client, *epoch, packet, sizeof(packet), &pb,
                            json, sizeof(json), &jb) == PET_XZ_OK && pb == 0);
    return client;
}

static void conversation_text(void)
{
    codec_boundary_t boundary={0}; uint32_t epoch;
    pet_xz_t client=waiting(&boundary,&epoch);
    pet_xz_event_t event;
    const char stt[]="{\"type\":\"stt\",\"text\":\"你好豆豆\"}";
    assert(pet_xz_receive_text(&client,epoch,stt,strlen(stt),&event)==PET_XZ_OK);
    assert(event.kind==PET_XZ_EVENT_TRANSCRIPT && !strcmp(event.text,"你好豆豆"));
    assert(pet_xz_receive_text(&client,epoch-1,stt,strlen(stt),&event)==PET_XZ_STALE);
    assert(event.text[0]==0);
}

static void response_start(pet_xz_t *client, uint32_t epoch)
{
    pet_xz_event_t event;
    const char text[] = "{\"type\":\"tts\",\"state\":\"start\"}";
    assert(pet_xz_receive_text(client, epoch, text, strlen(text), &event) == PET_XZ_OK);
    assert(event.kind == PET_XZ_EVENT_RESPONSE_BEGIN);
}

static void stale_connection_and_cancel(void)
{
    codec_boundary_t boundary = {0}; uint32_t epoch;
    pet_xz_t client = waiting(&boundary, &epoch);
    const uint32_t old = epoch;
    char json[PET_XZ_CONTROL_BYTES]; size_t bytes;
    assert(pet_xz_abort(&client, epoch, json, sizeof(json), &bytes) == PET_XZ_OK);
    assert(strcmp(json, "{\"session_id\":\"session-1\",\"type\":\"abort\"}") == 0);
    pet_xz_event_t event;
    assert(pet_xz_receive_text(&client, epoch, hello, strlen(hello), &event) == PET_XZ_NOT_READY);
    assert(pet_xz_begin(&client, 11, &epoch) == PET_XZ_BUSY);
    assert(pet_xz_close(&client, old) == PET_XZ_OK);
    assert(pet_xz_begin(&client, 10, &epoch) == PET_XZ_STALE);
    assert(pet_xz_begin(&client, 11, &epoch) == PET_XZ_OK && epoch == old + 1);
    assert(pet_xz_receive_text(&client, old, "broken", 6, &event) == PET_XZ_STALE);
    assert(event.kind == PET_XZ_EVENT_NONE);
    assert(pet_xz_receive_audio(&client, old, NULL, 0) == PET_XZ_STALE);
    assert(pet_xz_close(&client, old) == PET_XZ_STALE);
    start_input(&client, epoch); /* 旧回调没有破坏新握手。 */
    assert(boundary.resets == 2);
}

static void invalid_handshake_and_json(void)
{
    const char *bad[] = {
        "{}", "[]", "null", "{\"type\":1}", "{\"type\":\"hello\"}",
        "{\"type\":\"hello\",\"transport\":\"mqtt\"}",
        "{\"type\":\"ignored\",\"type\":\"hello\"}",
        "{\"type\":\"ignored\",\"t\\u0079pe\":\"hello\"}",
        "{\"type\":\"ignored\",\"nested\":{\"a\":1,\"a\":2}}",
        "{\"type\":\"tts\",\"state\":\"start\"}",
        "{\"type\":\"hel\\u0000lo\"}",
        "{\"type\":\"x\"}x", "{\"type\":\"x\"}{\"type\":\"x\"}",
        "{\"type\":\"x\n\"}", "{\"type\":\"\xc0\x80\"}", "{\"type\":\"\xed\xa0\x80\"}",
        "{\"type\":\"x\",\"a\":[[[[[[[[]]]]]]]]}",
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
        assert(pet_xz_receive_text(&client, epoch, bad[i], strlen(bad[i]), &event) == PET_XZ_INVALID);
        assert(pet_xz_receive_text(&client, epoch, hello, strlen(hello), &event) == PET_XZ_NOT_READY);
    }
    const char *fields[] = {"\"version\":2,", "\"version\":1.5,", "\"version\":\"1\","};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
        char text[PET_XZ_TEXT_BYTES]; snprintf(text, sizeof(text), "{%s%s", fields[i], hello + 1);
        assert(pet_xz_receive_text(&client, epoch, text, strlen(text), &event) == PET_XZ_INVALID);
    }
    const char *params[] = {
        "\"format\":\"pcm\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":60",
        "\"format\":\"opus\",\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60",
        "\"format\":\"opus\",\"sample_rate\":24000,\"channels\":2,\"frame_duration\":60",
        "\"format\":\"opus\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":20",
        "\"format\":\"opus\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":60.5",
        "\"format\":\"opus\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":\"60\"",
    };
    for (size_t i = 0; i < sizeof(params) / sizeof(params[0]); ++i) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
        char text[PET_XZ_TEXT_BYTES];
        snprintf(text, sizeof(text), "{\"type\":\"hello\",\"transport\":\"websocket\","
                 "\"session_id\":\"x\",\"audio_params\":{%s}}", params[i]);
        assert(pet_xz_receive_text(&client, epoch, text, strlen(text), &event) == PET_XZ_INVALID);
    }
    codec_boundary_t boundary = {0}; uint32_t epoch;
    pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
    char text[PET_XZ_TEXT_BYTES + 1]; memset(text, ' ', sizeof(text));
    assert(pet_xz_receive_text(&client, epoch, text, sizeof(text), &event) == PET_XZ_INVALID);
}

static void escaped_session_and_ignored_extensions(void)
{
    codec_boundary_t boundary = {0}; uint32_t epoch;
    pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
    char text[PET_XZ_TEXT_BYTES];
    snprintf(text, sizeof(text), "{\"type\":\"hello\",\"transport\":\"websocket\","
        "\"session_id\":\"x\\\"y\\\\z\",\"audio_params\":{\"format\":\"opus\",\"sample_rate\":24000,"
        "\"channels\":1,\"frame_duration\":60}}");
    assert(pet_xz_receive_text(&client, epoch, text, strlen(text), &event) == PET_XZ_OK);
    char json[PET_XZ_CONTROL_BYTES]; size_t bytes;
    assert(pet_xz_start_input(&client, epoch, json, 8, &bytes) == PET_XZ_INVALID && bytes == 0);
    assert(pet_xz_start_input(&client, epoch, json, sizeof(json), &bytes) == PET_XZ_OK);
    assert(strstr(json, "x\\\"y\\\\z") != NULL);
    const char extension[] = "{\"type\":\"iot\",\"payload\":{\"method\":\"ignored\"}}";
    assert(pet_xz_receive_text(&client, epoch, extension, strlen(extension), &event) == PET_XZ_IGNORED);
    assert(event.kind == PET_XZ_EVENT_NONE);
    const char tool[]="{\"type\":\"mcp\",\"payload\":{\"method\":\"tools/list\",\"id\":1,\"jsonrpc\":\"2.0\"}}";
    assert(pet_xz_receive_text(&client,epoch,tool,strlen(tool),&event)==PET_XZ_OK);
    assert(event.kind==PET_XZ_EVENT_TOOL&&strstr(event.rpc,"tools/list"));
    const char wrong[] = "{\"session_id\":\"other\",\"type\":\"error\"}";
    assert(pet_xz_receive_text(&client, epoch, wrong, strlen(wrong), &event) == PET_XZ_STALE);
    const char error[] = "{\"type\":\"error\"}";
    assert(pet_xz_receive_text(&client, epoch, error, strlen(error), &event) == PET_XZ_OK);
    assert(event.kind == PET_XZ_EVENT_REMOTE_FAILED && event.turn_id == 1);
    assert(pet_xz_start_input(&client, epoch, json, sizeof(json), &bytes) == PET_XZ_NOT_READY);
}

static void malformed_audio_and_codec_failures(void)
{
    const uint8_t bad[][2] = {{0x1c, 0}, {0x08, 0}, {0x19, 0}, {0xfb, 0}, {0xfb, 2}};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = waiting(&boundary, &epoch); response_start(&client, epoch);
        assert(pet_xz_receive_audio(&client, epoch, bad[i], sizeof(bad[i])) == PET_XZ_INVALID);
        assert(boundary.decoded == 0);
    }
    for (unsigned scenario = 0; scenario < 5; ++scenario) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = open_client(&boundary, 1, &epoch);
        if (scenario == 0) {
            assert(pet_xz_close(&client, epoch) == PET_XZ_OK);
            boundary.reset_fail = true;
            assert(pet_xz_begin(&client, 2, &epoch) == PET_XZ_CODEC_FAILED && epoch == 2);
            assert(pet_xz_close(&client, epoch) == PET_XZ_OK);
        } else if (scenario <= 2) {
            start_input(&client, epoch);
            boundary.encode_fail = scenario == 1; boundary.bad_encode_size = scenario == 2;
            uint8_t packet[PET_XZ_PACKET_BYTES]; size_t bytes;
            int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {0};
            for (unsigned i = 0; i < 2; ++i)
                assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES,
                                      packet, sizeof(packet), &bytes) == PET_XZ_OK);
            assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES,
                                  packet, sizeof(packet), &bytes) == PET_XZ_CODEC_FAILED && bytes == 0);
        } else {
            assert(pet_xz_close(&client, epoch) == PET_XZ_OK);
            client = waiting(&boundary, &epoch); response_start(&client, epoch);
            boundary.decode_fail = scenario == 3; boundary.bad_decode_size = scenario == 4;
            const uint8_t packet[] = {0x18, 1, 2};
            assert(pet_xz_receive_audio(&client, epoch, packet, sizeof(packet)) == PET_XZ_CODEC_FAILED);
            pet_xz_playback_t chunk;
            assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_NOT_READY && chunk.samples == 0);
        }
    }
}

static void encoded_packet_limit(void)
{
    codec_boundary_t boundary = {.encode_bytes = PET_XZ_PACKET_BYTES + 1}; uint32_t epoch;
    pet_xz_t client = open_client(&boundary, 1, &epoch); start_input(&client, epoch);
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {0};
    uint8_t packet[PET_XZ_PACKET_BYTES + 100]; size_t bytes;
    for (unsigned i = 0; i < 2; ++i)
        assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES,
                              packet, sizeof(packet), &bytes) == PET_XZ_OK);
    assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES,
                          packet, sizeof(packet), &bytes) == PET_XZ_CODEC_FAILED && bytes == 0);
}

static void truncation_and_pending_end(void)
{
    for (size_t bytes = 0; bytes < strlen(hello); ++bytes) {
        codec_boundary_t boundary = {0}; uint32_t epoch;
        pet_xz_t client = open_client(&boundary, 1, &epoch); pet_xz_event_t event;
        assert(pet_xz_receive_text(&client, epoch, hello, bytes, &event) == PET_XZ_INVALID);
    }
    codec_boundary_t boundary = {0}; uint32_t epoch;
    pet_xz_t client = waiting(&boundary, &epoch); response_start(&client, epoch);
    const uint8_t packet[] = {0xfb, 3, 1, 2, 3}; /* 三个 20 ms CELT 帧的 TOC。 */
    assert(pet_xz_receive_audio(&client, epoch, packet, sizeof(packet)) == PET_XZ_OK);
    pet_xz_event_t event;
    const char stop[] = "{\"type\":\"tts\",\"state\":\"stop\"}";
    assert(pet_xz_receive_text(&client, epoch, stop, strlen(stop), &event) == PET_XZ_OK);
    assert(pet_xz_receive_text(&client, epoch, stop, strlen(stop), &event) == PET_XZ_IGNORED);
    pet_xz_playback_t chunk;
    assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_INVALID);
    for (unsigned i = 0; i < 3; ++i) {
        assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_OK && !chunk.finish);
        assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_OK);
        assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_INVALID);
    }
    assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_OK && chunk.finish);
    assert(pet_xz_commit_playback(&client, epoch) == PET_XZ_OK);
    assert(pet_xz_close(&client, epoch) == PET_XZ_OK);
    assert(pet_xz_begin(&client, 12, &epoch) == PET_XZ_OK);
    start_input(&client, epoch);
    uint8_t out[PET_XZ_PACKET_BYTES]; size_t bytes;
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES];
    for (size_t i = 0; i < PET_AUDIO_CAPTURE_SAMPLES; ++i) pcm[i] = 123;
    assert(pet_xz_capture(&client, epoch, pcm, PET_AUDIO_CAPTURE_SAMPLES, out, sizeof(out), &bytes) == PET_XZ_OK);
    assert(pet_xz_capture(&client, epoch, pcm, 0, out, sizeof(out), &bytes) == PET_XZ_INVALID);
    char json[PET_XZ_CONTROL_BYTES]; size_t jb;
    assert(pet_xz_abort(&client, epoch, json, sizeof(json), &jb) == PET_XZ_OK);
    assert(pet_xz_close(&client, epoch) == PET_XZ_OK);
    assert(pet_xz_begin(&client, 13, &epoch) == PET_XZ_OK);
    start_input(&client, epoch);
    assert(pet_xz_end_input(&client, epoch, out, sizeof(out), &bytes, json, sizeof(json), &jb) == PET_XZ_OK);
    assert(bytes == 0); /* 取消轮次中暂存的麦克风 PCM 未串入新轮次。 */
    response_start(&client, epoch);
    assert(pet_xz_receive_text(&client, epoch, stop, strlen(stop), &event) == PET_XZ_INVALID);
    assert(pet_xz_peek_playback(&client, epoch, &chunk) == PET_XZ_NOT_READY);
}

int main(void)
{
    conversation_text();
    handshake();
    capture_and_playback();
    stale_connection_and_cancel();
    invalid_handshake_and_json();
    escaped_session_and_ignored_extensions();
    malformed_audio_and_codec_failures();
    encoded_packet_limit();
    truncation_and_pending_end();
    puts("Xiaozhi: v1 handshake and manual local-endpoint controls passed");
    puts("Xiaozhi: 20/60 ms capture aggregation, final padding and lossless playback backpressure passed");
    puts("Xiaozhi: stale epochs, cancellation, malformed JSON/Opus and codec failures passed");
}
