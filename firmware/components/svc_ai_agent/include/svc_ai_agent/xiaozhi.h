#pragma once

#include "svc_ai_agent/session.h"

/* 03 默认的 WebSocket v1：一条物理连接只承载一个本地轮次。 */
#define PET_XZ_TEXT_BYTES 1024u
#define PET_XZ_CONTROL_BYTES 512u
#define PET_XZ_PACKET_BYTES 1500u
#define PET_XZ_SESSION_BYTES 64u
#define PET_XZ_ENCODE_SAMPLES 960u
#define PET_XZ_DECODE_SAMPLES 1440u

typedef enum {
    PET_XZ_OK,
    PET_XZ_IGNORED,
    PET_XZ_STALE,
    PET_XZ_INVALID,
    PET_XZ_NOT_READY,
    PET_XZ_BUSY,
    PET_XZ_CODEC_FAILED,
    PET_XZ_ID_EXHAUSTED,
} pet_xz_result_t;

typedef struct {
    void *context;
    bool (*reset)(void *context);
    /* 同步消费；编码 16 kHz/960 样本，解码输出 24 kHz/1440 样本。 */
    bool (*encode)(void *context, const int16_t *pcm, uint8_t *out, size_t capacity,
                   size_t *bytes);
    bool (*decode)(void *context, const uint8_t *packet, size_t bytes, int16_t *out,
                   size_t capacity, size_t *samples);
} pet_xz_codec_t;

typedef enum {
    PET_XZ_CLOSED,
    PET_XZ_HELLO,
    PET_XZ_READY,
    PET_XZ_INPUT,
    PET_XZ_WAITING,
    PET_XZ_OUTPUT,
    PET_XZ_DONE,
    PET_XZ_FAILED,
} pet_xz_phase_t;

typedef enum {
    PET_XZ_EVENT_NONE,
    PET_XZ_EVENT_HELLO,
    PET_XZ_EVENT_RESPONSE_BEGIN,
    PET_XZ_EVENT_RESPONSE_END,
    PET_XZ_EVENT_REMOTE_FAILED,
    PET_XZ_EVENT_TRANSCRIPT,
    PET_XZ_EVENT_REPLY,
    PET_XZ_EVENT_TOOL,
} pet_xz_event_kind_t;

typedef struct {
    pet_xz_event_kind_t kind;
    uint32_t epoch, turn_id;
    char text[PET_AI_TEXT_MAX+1];
    char rpc[PET_XZ_TEXT_BYTES+1];
} pet_xz_event_t;

typedef struct {
    const int16_t *pcm;
    size_t samples;
    bool finish;
} pet_xz_playback_t;

/* 单一网络/codec 拥有者串行调用，不可直接作为 A4 音频任务的阻塞回调。 */
typedef struct {
    pet_xz_codec_t codec;
    pet_xz_phase_t phase;
    uint32_t epoch, turn_id;
    char session_id[PET_XZ_SESSION_BYTES + 1];
    int16_t capture[PET_XZ_ENCODE_SAMPLES];
    int16_t playback[PET_XZ_DECODE_SAMPLES];
    size_t capture_fill, playback_offset, playback_samples;
    bool ended, have_audio, offered;
} pet_xz_t;

/* 实例生命周期只 init 一次；重新 init 会失去代次，必须先销毁全部旧连接/回调。 */
bool pet_xz_init(pet_xz_t *client, const pet_xz_codec_t *codec);
const char *pet_xz_hello(void);
const char *pet_xz_hello_tools(bool enabled);

/* begin 后创建全新物理连接，接收回调永久绑定返回的 epoch；不允许自动重连复用。
 * 上一条连接关闭并撤销回调后才能 close/begin。turn_id 严格递增；epoch 不回绕。
 * 本库不创建连接、任务或超时器，不保存凭据/转写文本，不自动调用 A4。
 */
pet_xz_result_t pet_xz_begin(pet_xz_t *client, uint32_t turn_id, uint32_t *epoch);
pet_xz_result_t pet_xz_close(pet_xz_t *client, uint32_t epoch);

/* 输入为一个完整 WebSocket 文本消息（不含终止 NUL）；分片重组由网络拥有者负责。 */
pet_xz_result_t pet_xz_receive_text(pet_xz_t *client, uint32_t epoch,
                                   const char *json, size_t bytes, pet_xz_event_t *event);
pet_xz_result_t pet_xz_start_input(pet_xz_t *client, uint32_t epoch,
                                  char *json, size_t capacity, size_t *bytes);
pet_xz_result_t pet_xz_capture(pet_xz_t *client, uint32_t epoch,
                              const int16_t *pcm, size_t samples,
                              uint8_t *packet, size_t capacity, size_t *bytes);
/* 最后不足 60 ms 的输入补最多 40 ms 静音；先发送非空 packet，再发送 stop JSON。
 * 输出只表示已编码，发送失败必须关闭本轮，不能重试已消费的 PCM。
 */
pet_xz_result_t pet_xz_end_input(pet_xz_t *client, uint32_t epoch,
                                uint8_t *packet, size_t packet_capacity, size_t *packet_bytes,
                                char *json, size_t json_capacity, size_t *json_bytes);
pet_xz_result_t pet_xz_abort(pet_xz_t *client, uint32_t epoch,
                            char *json, size_t capacity, size_t *bytes);

/* 仅接受完整的单声道 60 ms Opus；BUSY 时尚未解码，调用方保留原消息并稍后重试。 */
pet_xz_result_t pet_xz_receive_audio(pet_xz_t *client, uint32_t epoch,
                                    const uint8_t *packet, size_t bytes);
/* peek 不消费；A4 play 成功入队后再 commit。队满保留当前 20 ms 块，不重复解码。
 * RESPONSE_BEGIN 须先成功投递 A4；finish 只是输入结束，不是 DMA drained。
 */
pet_xz_result_t pet_xz_peek_playback(pet_xz_t *client, uint32_t epoch,
                                    pet_xz_playback_t *playback);
pet_xz_result_t pet_xz_commit_playback(pet_xz_t *client, uint32_t epoch);
