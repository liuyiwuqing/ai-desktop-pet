#include "svc_ai_agent/xiaozhi.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static bool initialized(const pet_xz_t *client)
{
    return client != NULL && client->codec.reset != NULL && client->codec.encode != NULL &&
           client->codec.decode != NULL;
}

static pet_xz_result_t scope(const pet_xz_t *client, uint32_t epoch)
{
    if (!initialized(client)) return PET_XZ_NOT_READY;
    if (epoch == 0 || epoch != client->epoch) return PET_XZ_STALE;
    if (client->phase == PET_XZ_CLOSED || client->phase == PET_XZ_FAILED)
        return PET_XZ_NOT_READY;
    return PET_XZ_OK;
}

static void clear_pcm(pet_xz_t *client)
{
    memset(client->capture, 0, sizeof(client->capture));
    memset(client->playback, 0, sizeof(client->playback));
    client->capture_fill = client->playback_offset = client->playback_samples = 0;
    client->offered = false;
}

static pet_xz_result_t failed(pet_xz_t *client, pet_xz_result_t result)
{
    clear_pcm(client);
    client->phase = PET_XZ_FAILED;
    return result;
}

bool pet_xz_init(pet_xz_t *client, const pet_xz_codec_t *codec)
{
    if (client == NULL || codec == NULL || codec->reset == NULL || codec->encode == NULL ||
        codec->decode == NULL) return false;
    const pet_xz_codec_t copy = *codec;
    memset(client, 0, sizeof(*client));
    client->codec = copy;
    return true;
}

const char *pet_xz_hello(void)
{
    return "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
           "\"features\":{\"mcp\":false},\"audio_params\":{\"format\":\"opus\","
           "\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}";
}

const char *pet_xz_hello_tools(bool enabled)
{
    return enabled ? "{\"type\":\"hello\",\"version\":1,\"transport\":\"websocket\","
        "\"features\":{\"mcp\":true},\"audio_params\":{\"format\":\"opus\","
        "\"sample_rate\":16000,\"channels\":1,\"frame_duration\":60}}" : pet_xz_hello();
}

pet_xz_result_t pet_xz_begin(pet_xz_t *client, uint32_t turn_id, uint32_t *epoch)
{
    if (epoch != NULL) *epoch = 0;
    if (!initialized(client)) return PET_XZ_NOT_READY;
    if (epoch == NULL || turn_id == 0) return PET_XZ_INVALID;
    if (client->phase != PET_XZ_CLOSED) return PET_XZ_BUSY;
    if (client->epoch == UINT32_MAX) return PET_XZ_ID_EXHAUSTED;
    if (turn_id <= client->turn_id) return PET_XZ_STALE;
    ++client->epoch;
    client->turn_id = turn_id;
    clear_pcm(client);
    client->session_id[0] = '\0';
    client->ended = client->have_audio = false;
    *epoch = client->epoch;
    if (!client->codec.reset(client->codec.context)) return failed(client, PET_XZ_CODEC_FAILED);
    client->phase = PET_XZ_HELLO;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_close(pet_xz_t *client, uint32_t epoch)
{
    if (!initialized(client)) return PET_XZ_NOT_READY;
    if (epoch == 0 || epoch != client->epoch) return PET_XZ_STALE;
    clear_pcm(client);
    memset(client->session_id, 0, sizeof(client->session_id));
    client->ended = client->have_audio = false;
    client->phase = PET_XZ_CLOSED;
    return PET_XZ_OK;
}

/* 在分配 JSON 树前限制深度和 UTF-8；cJSON 的 C 字符串不能表示嵌入 NUL。 */
static bool bounded_json(const char *json, size_t bytes)
{
    if (json == NULL || bytes == 0 || bytes > PET_XZ_TEXT_BYTES) return false;
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < bytes; ++i) {
        const unsigned char c = (unsigned char)json[i];
        if (c == 0 || (c < 0x20 && (quoted || (c != '\t' && c != '\r' && c != '\n'))))
            return false;
        if (c >= 0x80) {
            unsigned follow;
            uint32_t cp, minimum;
            if (c >= 0xc2 && c <= 0xdf) { follow = 1; cp = c & 0x1f; minimum = 0x80; }
            else if (c >= 0xe0 && c <= 0xef) { follow = 2; cp = c & 0x0f; minimum = 0x800; }
            else if (c >= 0xf0 && c <= 0xf4) { follow = 3; cp = c & 0x07; minimum = 0x10000; }
            else return false;
            if (follow >= bytes - i) return false;
            while (follow-- != 0) {
                const unsigned char next = (unsigned char)json[++i];
                if ((next & 0xc0) != 0x80) return false;
                cp = (cp << 6) | (next & 0x3f);
            }
            if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
            continue;
        }
        if (quoted && c == '\\') {
            if (++i >= bytes) return false;
            if (json[i] == 'u' && bytes - i >= 5 && memcmp(json + i + 1, "0000", 4) == 0)
                return false;
            continue;
        }
        if (c == '"') quoted = !quoted;
        else if (!quoted && (c == '{' || c == '[')) { if (++depth > 8) return false; }
        else if (!quoted && (c == '}' || c == ']')) { if (depth == 0) return false; --depth; }
    }
    return !quoted && depth == 0;
}

static bool unique_keys(const cJSON *node)
{
    if (cJSON_IsObject(node)) {
        for (const cJSON *a = node->child; a != NULL; a = a->next)
            for (const cJSON *b = a->next; b != NULL; b = b->next)
                if (strcmp(a->string, b->string) == 0) return false;
    }
    for (const cJSON *child = node->child; child != NULL; child = child->next)
        if (!unique_keys(child)) return false;
    return true;
}

static const cJSON *field(const cJSON *node, const char *key)
{
    return cJSON_GetObjectItemCaseSensitive(node, key);
}
static bool string_is(const cJSON *node, const char *expected)
{
    return cJSON_IsString(node) && strcmp(node->valuestring, expected) == 0;
}
static bool number_is(const cJSON *node, double expected)
{
    return cJSON_IsNumber(node) && node->valuedouble == expected;
}

static pet_xz_result_t text_event(const cJSON *root,pet_xz_event_t *event,pet_xz_event_kind_t kind)
{
    const cJSON *text=field(root,"text");
    if (!cJSON_IsString(text)) return PET_XZ_INVALID;
    size_t n=strlen(text->valuestring);
    if (n>PET_AI_TEXT_MAX) {
        n=PET_AI_TEXT_MAX;
        while (n && ((unsigned char)text->valuestring[n]&0xc0)==0x80) --n;
    }
    memcpy(event->text,text->valuestring,n); event->text[n]=0; event->kind=kind;
    return PET_XZ_OK;
}

static pet_xz_result_t receive_object(pet_xz_t *client, const cJSON *root, pet_xz_event_t *event)
{
    const cJSON *type = field(root, "type");
    if (!cJSON_IsString(type)) return PET_XZ_INVALID;
    if (string_is(type, "hello")) {
        const cJSON *audio = field(root, "audio_params"), *sid = field(root, "session_id");
        const cJSON *version = field(root, "version");
        if (client->phase != PET_XZ_HELLO || !string_is(field(root, "transport"), "websocket") ||
            (version != NULL && !number_is(version, 1)) || !cJSON_IsObject(audio) ||
            !string_is(field(audio, "format"), "opus") ||
            !number_is(field(audio, "sample_rate"), PET_AUDIO_PLAYBACK_RATE) ||
            !number_is(field(audio, "channels"), 1) || !number_is(field(audio, "frame_duration"), 60) ||
            !cJSON_IsString(sid) || sid->valuestring[0] == '\0' ||
            strlen(sid->valuestring) > PET_XZ_SESSION_BYTES) return PET_XZ_INVALID;
        strcpy(client->session_id, sid->valuestring);
        client->phase = PET_XZ_READY;
        event->kind = PET_XZ_EVENT_HELLO;
        return PET_XZ_OK;
    }
    const cJSON *sid = field(root, "session_id");
    if (sid != NULL && (!cJSON_IsString(sid) || strcmp(sid->valuestring, client->session_id) != 0))
        return PET_XZ_STALE;
    if (string_is(type, "error") || string_is(type, "goodbye")) {
        (void)failed(client, PET_XZ_INVALID);
        event->kind = PET_XZ_EVENT_REMOTE_FAILED;
        return PET_XZ_OK;
    }
    if (string_is(type,"mcp")) {
        const cJSON *payload=field(root,"payload");
        if (!cJSON_IsObject(payload)||!cJSON_PrintPreallocated((cJSON *)payload,event->rpc,sizeof(event->rpc),false))
            return PET_XZ_INVALID;
        event->kind=PET_XZ_EVENT_TOOL; return PET_XZ_OK;
    }
    if (string_is(type,"stt")) {
        if (client->phase!=PET_XZ_INPUT && client->phase!=PET_XZ_WAITING && client->phase!=PET_XZ_OUTPUT)
            return PET_XZ_IGNORED;
        return text_event(root,event,PET_XZ_EVENT_TRANSCRIPT);
    }
    if (string_is(type, "tts")) {
        const cJSON *state = field(root, "state");
        if (!cJSON_IsString(state)) return PET_XZ_INVALID;
        if (string_is(state, "start")) {
            if (client->phase == PET_XZ_OUTPUT && !client->ended) return PET_XZ_IGNORED;
            if (client->phase != PET_XZ_WAITING) return PET_XZ_INVALID;
            client->phase = PET_XZ_OUTPUT;
            event->kind = PET_XZ_EVENT_RESPONSE_BEGIN;
            return PET_XZ_OK;
        }
        if (string_is(state, "stop")) {
            if ((client->phase == PET_XZ_OUTPUT && client->ended) || client->phase == PET_XZ_DONE)
                return PET_XZ_IGNORED;
            if (client->phase != PET_XZ_OUTPUT || !client->have_audio) return PET_XZ_INVALID;
            client->ended = true;
            event->kind = PET_XZ_EVENT_RESPONSE_END;
            return PET_XZ_OK;
        }
        if (string_is(state,"sentence_start")) {
            if (client->phase!=PET_XZ_OUTPUT || client->ended) return PET_XZ_IGNORED;
            return text_event(root,event,PET_XZ_EVENT_REPLY);
        }
        if (string_is(state,"sentence_end")) return PET_XZ_IGNORED;
        return PET_XZ_INVALID;
    }
    return PET_XZ_IGNORED;
}

pet_xz_result_t pet_xz_receive_text(pet_xz_t *client, uint32_t epoch,
                                   const char *json, size_t bytes, pet_xz_event_t *event)
{
    if (event != NULL) memset(event, 0, sizeof(*event));
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (event == NULL) return PET_XZ_INVALID;
    if (!bounded_json(json, bytes)) return failed(client, PET_XZ_INVALID);
    const char *end = NULL;
    cJSON *root = cJSON_ParseWithLengthOpts(json, bytes, &end, false);
    if (root != NULL) {
        while (end < json + bytes && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    }
    if (!cJSON_IsObject(root) || end != json + bytes || !unique_keys(root)) {
        cJSON_Delete(root);
        return failed(client, PET_XZ_INVALID);
    }
    const pet_xz_result_t result = receive_object(client, root, event);
    cJSON_Delete(root);
    if (result == PET_XZ_INVALID) return failed(client, result);
    if (result == PET_XZ_OK) { event->epoch = epoch; event->turn_id = client->turn_id; }
    return result;
}

static pet_xz_result_t control(const pet_xz_t *client, const char *fields,
                               char *json, size_t capacity, size_t *bytes)
{
    if (bytes != NULL) *bytes = 0;
    if (json == NULL || bytes == NULL || capacity < PET_XZ_CONTROL_BYTES) return PET_XZ_INVALID;
    char escaped[PET_XZ_SESSION_BYTES * 6 + 1];
    size_t fill = 0;
    for (const unsigned char *p = (const unsigned char *)client->session_id; *p; ++p) {
        if (*p == '"' || *p == '\\') { escaped[fill++] = '\\'; escaped[fill++] = (char)*p; }
        else if (*p < 0x20) { (void)snprintf(escaped + fill, 7, "\\u%04x", *p); fill += 6; }
        else escaped[fill++] = (char)*p;
    }
    escaped[fill] = '\0';
    const int length = snprintf(json, capacity, "{\"session_id\":\"%s\",%s}", escaped, fields);
    if (length < 0 || (size_t)length >= capacity) return PET_XZ_INVALID;
    *bytes = (size_t)length;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_start_input(pet_xz_t *client, uint32_t epoch,
                                  char *json, size_t capacity, size_t *bytes)
{
    if (bytes != NULL) *bytes = 0;
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase != PET_XZ_READY) return PET_XZ_NOT_READY;
    const pet_xz_result_t result = control(client, "\"type\":\"listen\",\"state\":\"start\",\"mode\":\"manual\"",
                                          json, capacity, bytes);
    if (result == PET_XZ_OK) client->phase = PET_XZ_INPUT;
    return result;
}

static pet_xz_result_t encode_capture(pet_xz_t *client, uint8_t *packet,
                                     size_t capacity, size_t *bytes)
{
    if (!client->codec.encode(client->codec.context, client->capture, packet, capacity, bytes) ||
        *bytes == 0 || *bytes > capacity || *bytes > PET_XZ_PACKET_BYTES) {
        *bytes = 0;
        return failed(client, PET_XZ_CODEC_FAILED);
    }
    memset(client->capture, 0, sizeof(client->capture));
    client->capture_fill = 0;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_capture(pet_xz_t *client, uint32_t epoch,
                              const int16_t *pcm, size_t samples,
                              uint8_t *packet, size_t capacity, size_t *bytes)
{
    if (bytes != NULL) *bytes = 0;
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase != PET_XZ_INPUT) return PET_XZ_NOT_READY;
    if (pcm == NULL || samples != PET_AUDIO_CAPTURE_SAMPLES || packet == NULL ||
        capacity < PET_XZ_PACKET_BYTES || bytes == NULL) return PET_XZ_INVALID;
    memcpy(client->capture + client->capture_fill, pcm, samples * sizeof(*pcm));
    client->capture_fill += samples;
    if (client->capture_fill < PET_XZ_ENCODE_SAMPLES) return PET_XZ_OK;
    return encode_capture(client, packet, capacity, bytes);
}

pet_xz_result_t pet_xz_end_input(pet_xz_t *client, uint32_t epoch,
                                uint8_t *packet, size_t packet_capacity, size_t *packet_bytes,
                                char *json, size_t json_capacity, size_t *json_bytes)
{
    if (packet_bytes != NULL) *packet_bytes = 0;
    if (json_bytes != NULL) *json_bytes = 0;
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase != PET_XZ_INPUT) return PET_XZ_NOT_READY;
    if (packet == NULL || packet_bytes == NULL || packet_capacity < PET_XZ_PACKET_BYTES)
        return PET_XZ_INVALID;
    pet_xz_result_t result = control(client, "\"type\":\"listen\",\"state\":\"stop\"",
                                    json, json_capacity, json_bytes);
    if (result != PET_XZ_OK) return result;
    if (client->capture_fill != 0) {
        memset(client->capture + client->capture_fill, 0,
               (PET_XZ_ENCODE_SAMPLES - client->capture_fill) * sizeof(int16_t));
        result = encode_capture(client, packet, packet_capacity, packet_bytes);
        if (result != PET_XZ_OK) { *json_bytes = 0; return result; }
    }
    client->phase = PET_XZ_WAITING;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_abort(pet_xz_t *client, uint32_t epoch,
                            char *json, size_t capacity, size_t *bytes)
{
    if (bytes != NULL) *bytes = 0;
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase == PET_XZ_HELLO) return PET_XZ_NOT_READY;
    const pet_xz_result_t result = control(client, "\"type\":\"abort\"", json, capacity, bytes);
    if (result == PET_XZ_OK) (void)failed(client, PET_XZ_OK);
    return result;
}

/* RFC 6716 TOC：先拒绝非 mono/60 ms，再交给真实 codec 校验包内长度与编码数据。 */
static bool opus_60ms_mono(const uint8_t *packet, size_t bytes)
{
    if (packet == NULL || bytes == 0 || bytes > PET_XZ_PACKET_BYTES || (packet[0] & 4) != 0)
        return false;
    const unsigned config = packet[0] >> 3;
    unsigned samples;
    if (config < 12) {
        static const unsigned silk[] = {480, 960, 1920, 2880};
        samples = silk[config & 3];
    } else if (config < 16) samples = (config & 1) ? 960 : 480;
    else samples = 120u << (config & 3);
    unsigned frames = 1;
    const unsigned code = packet[0] & 3;
    if (code == 1 || code == 2) frames = 2;
    else if (code == 3) {
        if (bytes < 2) return false;
        frames = packet[1] & 0x3f;
    }
    return samples * frames == 2880;
}

pet_xz_result_t pet_xz_receive_audio(pet_xz_t *client, uint32_t epoch,
                                    const uint8_t *packet, size_t bytes)
{
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase != PET_XZ_OUTPUT || client->ended) return failed(client, PET_XZ_INVALID);
    if (client->playback_samples != 0) return PET_XZ_BUSY;
    if (!opus_60ms_mono(packet, bytes)) return failed(client, PET_XZ_INVALID);
    size_t samples = 0;
    if (!client->codec.decode(client->codec.context, packet, bytes, client->playback,
                               PET_XZ_DECODE_SAMPLES, &samples) || samples != PET_XZ_DECODE_SAMPLES)
        return failed(client, PET_XZ_CODEC_FAILED);
    client->playback_samples = samples;
    client->playback_offset = 0;
    client->have_audio = true;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_peek_playback(pet_xz_t *client, uint32_t epoch,
                                    pet_xz_playback_t *playback)
{
    if (playback != NULL) memset(playback, 0, sizeof(*playback));
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (playback == NULL) return PET_XZ_INVALID;
    if (client->phase != PET_XZ_OUTPUT) return PET_XZ_IGNORED;
    if (client->playback_samples != 0) {
        playback->pcm = client->playback + client->playback_offset;
        playback->samples = PET_AUDIO_PLAYBACK_SAMPLES;
    } else if (client->ended) playback->finish = true;
    else return PET_XZ_IGNORED;
    client->offered = true;
    return PET_XZ_OK;
}

pet_xz_result_t pet_xz_commit_playback(pet_xz_t *client, uint32_t epoch)
{
    const pet_xz_result_t check = scope(client, epoch);
    if (check != PET_XZ_OK) return check;
    if (client->phase != PET_XZ_OUTPUT || !client->offered) return PET_XZ_INVALID;
    if (client->playback_samples != 0) {
        memset(client->playback + client->playback_offset, 0, PET_AUDIO_PLAYBACK_SAMPLES * sizeof(int16_t));
        client->playback_offset += PET_AUDIO_PLAYBACK_SAMPLES;
        if (client->playback_offset == client->playback_samples)
            client->playback_samples = client->playback_offset = 0;
    } else if (client->ended) client->phase = PET_XZ_DONE;
    else return PET_XZ_INVALID;
    client->offered = false;
    return PET_XZ_OK;
}
