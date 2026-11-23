#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "svc_ai_agent/wss.h"
#include "svc_intent/api.h"
#include "ai_startup.h"
#include "app_tools.h"
#include "svc_focus/api.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include "esp_websocket_client.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"

/* 真实 A4/A5/WSS + SDK/OS/I2S 边界替身；不接网络，不执行 vendor Opus 二进制。 */
static const char *scenario;
static bool failure_armed = true, wifi = true, synced = true;
static unsigned allocations, queues, queue_calls, codec_opens, socket_inits, socket_destroys;
static char last_text[8192];
static unsigned tool_executions;
static bool delayed_write;
static pet_intent_rpc_t intents;
static cJSON *count_tool(const cJSON *args,void *context)
{
    (void)args;(void)context;
    ++tool_executions;
    cJSON *out=cJSON_CreateObject();cJSON_AddNumberToObject(out,"count",tool_executions);return out;
}
static bool tool_handler(uint32_t epoch,const char *request,size_t bytes,char *out,size_t cap,
                         size_t *written,void *context)
{
    assert(epoch==1&&context==&intents);
    return pet_intent_rpc(context,request,bytes,out,cap,written);
}
/* Focus/message services are outside this protocol fixture; no external action is executed. */
uint8_t pet_message_hub_depth(void) { return 2; }
esp_err_t app_focus_command(const char *action) { (void)action; return ESP_ERR_INVALID_STATE; }
esp_err_t pet_focus_snapshot(pet_focus_t *out) { (void)out; return ESP_ERR_INVALID_STATE; }
static bool is_case(const char *name) { return strcmp(scenario, name) == 0; }
static bool failing(const char *name) { return failure_armed && is_case(name); }
static atomic_uint_fast64_t clock_ms;

typedef struct {
    pthread_t thread;
    TaskFunction_t function;
    void *context;
    const char *name;
    bool paused, notified, permit, dead;
} host_task_t;
static host_task_t tasks[3];
static unsigned task_count;
static pthread_mutex_t scheduler = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static host_task_t *current_task(void)
{
    for (unsigned i = 0; i < task_count; ++i)
        if (pthread_equal(pthread_self(), tasks[i].thread)) return &tasks[i];
    return NULL;
}
static host_task_t *named_task(const char *name)
{
    for (unsigned i = 0; i < task_count; ++i)
        if (strcmp(tasks[i].name, name) == 0) return &tasks[i];
    assert(!"task missing");
    return NULL;
}
static void wait_change(void)
{
    struct timespec limit;
    timespec_get(&limit, TIME_UTC);
    limit.tv_sec += 5;
    assert(pthread_cond_timedwait(&changed, &scheduler, &limit) == 0);
}
static void *task_entry(void *context)
{
    host_task_t *task = context;
    task->function(task->context);
    assert(!"task must end through vTaskDelete");
    return NULL;
}
static void step(const char *name)
{
    host_task_t *task = named_task(name);
    pthread_mutex_lock(&scheduler);
    assert(task->paused && !task->dead);
    atomic_fetch_add(&clock_ms, 5);
    task->paused = false;
    task->permit = true;
    pthread_cond_broadcast(&changed);
    while (!task->paused && !task->dead) wait_change();
    pthread_mutex_unlock(&scheduler);
}
int64_t esp_timer_get_time(void) { return (int64_t)atomic_load(&clock_ms) * 1000; }
void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert((caps & MALLOC_CAP_8BIT) != 0);
    if (failing("alloc-fail")) return NULL;
    void *value = calloc(1, size);
    if (value != NULL) ++allocations;
    return value;
}
void heap_caps_free(void *value) { assert(allocations > 0); --allocations; free(value); }
typedef struct {
    pthread_mutex_t lock;
    size_t size;
    unsigned capacity, head, count;
    unsigned char *items;
} host_queue_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned size)
{
    ++queue_calls;
    if (failing("tx-queue-fail") || (failing("rx-queue-fail") && queue_calls == 2)) return NULL;
    host_queue_t *queue = calloc(1, sizeof(*queue));
    assert(queue != NULL);
    queue->items = calloc(length, size);
    assert(queue->items != NULL);
    assert(pthread_mutex_init(&queue->lock, NULL) == 0);
    queue->capacity = length;
    queue->size = size;
    ++queues;
    return queue;
}
QueueHandle_t xQueueCreateWithCaps(unsigned length, unsigned size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    return xQueueCreate(length, size);
}
void vQueueDelete(QueueHandle_t handle)
{
    host_queue_t *queue = handle;
    assert(queues > 0);
    --queues;
    pthread_mutex_destroy(&queue->lock);
    free(queue->items);
    free(queue);
}
void vQueueDeleteWithCaps(QueueHandle_t handle) { vQueueDelete(handle); }
int xQueueSend(QueueHandle_t handle, const void *item, TickType_t wait)
{
    assert(wait == 0);
    host_queue_t *queue = handle;
    pthread_mutex_lock(&queue->lock);
    if (queue->count == queue->capacity) { pthread_mutex_unlock(&queue->lock); return 0; }
    memcpy(queue->items + (queue->head + queue->count++) % queue->capacity * queue->size,
           item, queue->size);
    pthread_mutex_unlock(&queue->lock);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle, void *item, TickType_t wait)
{
    assert(wait == 0);
    host_queue_t *queue = handle;
    pthread_mutex_lock(&queue->lock);
    if (queue->count == 0) { pthread_mutex_unlock(&queue->lock); return 0; }
    memcpy(item, queue->items + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    pthread_mutex_unlock(&queue->lock);
    return pdTRUE;
}
int xQueueReset(QueueHandle_t handle)
{
    host_queue_t *queue = handle;
    pthread_mutex_lock(&queue->lock);
    queue->head = queue->count = 0;
    pthread_mutex_unlock(&queue->lock);
    return pdTRUE;
}
int xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *context,
                unsigned priority, TaskHandle_t *handle)
{
    assert(stack >= 4096 && priority > 0 && handle != NULL);
    if (failing("task-fail")) return 0;
    assert(task_count < 3);
    host_task_t *task = &tasks[task_count++];
    *task = (host_task_t){.name = name, .function = function, .context = context};
    pthread_mutex_lock(&scheduler);
    assert(pthread_create(&task->thread, NULL, task_entry, task) == 0);
    assert(pthread_detach(task->thread) == 0); /* 对应 FreeRTOS 自删除任务，无 join 句柄。 */
    while (!task->paused) wait_change();
    pthread_mutex_unlock(&scheduler);
    *handle = task;
    return pdPASS;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t wait)
{
    assert(clear == pdTRUE && wait == portMAX_DELAY);
    pthread_mutex_lock(&scheduler);
    host_task_t *task = current_task();
    assert(task != NULL);
    task->paused = true;
    pthread_cond_broadcast(&changed);
    while (!task->notified || !task->permit) wait_change();
    task->permit = false;
    pthread_mutex_unlock(&scheduler);
    return 1;
}
int xTaskNotifyGive(TaskHandle_t handle)
{
    host_task_t *task = handle;
    pthread_mutex_lock(&scheduler);
    task->notified = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&scheduler);
    return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    assert(ticks > 0);
    pthread_mutex_lock(&scheduler);
    host_task_t *task = current_task();
    assert(task != NULL);
    task->paused = true;
    pthread_cond_broadcast(&changed);
    while (!task->permit) wait_change();
    task->permit = false;
    pthread_mutex_unlock(&scheduler);
}
void taskYield(void) { vTaskDelay(1); }
void vTaskDelete(TaskHandle_t handle)
{
    assert(handle == NULL);
    pthread_mutex_lock(&scheduler);
    host_task_t *task = current_task();
    assert(task != NULL);
    task->dead = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&scheduler);
    pthread_exit(NULL);
}

bool pet_net_is_time_synced(void) { return synced; }
esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key)
{
    assert(strcmp(key, "WIFI_STA_DEF") == 0);
    return wifi ? (esp_netif_t *)&wifi : NULL;
}
bool esp_netif_is_netif_up(esp_netif_t *netif) { assert(netif != NULL); return wifi; }
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif, esp_netif_ip_info_t *info)
{
    assert(netif != NULL);
    info->ip.addr = wifi ? 0x01020304 : 0;
    return ESP_OK;
}
esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }

static int encoder_token, decoder_token;
static unsigned encoded;
static unsigned decoded;
esp_audio_err_t esp_opus_enc_open(void *config, uint32_t bytes, void **handle)
{
    assert(current_task() == named_task("pet_xz_wss") && config && bytes > 0);
    ++codec_opens;
    *handle = failing("codec-fail") ? NULL : &encoder_token;
    return *handle != NULL ? ESP_AUDIO_ERR_OK : ESP_AUDIO_ERR_MEM_LACK;
}
esp_audio_err_t esp_opus_dec_open(void *config, uint32_t bytes, void **handle)
{
    assert(current_task() == named_task("pet_xz_wss") && config && bytes > 0);
    *handle = &decoder_token;
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_get_frame_size(void *handle, int *input, int *output)
{
    assert(handle == &encoder_token); *input = 1920; *output = 1275; return ESP_AUDIO_ERR_OK;
}
void esp_opus_enc_close(void *handle) { assert(handle == &encoder_token); }
esp_audio_err_t esp_opus_dec_close(void *handle)
{
    assert(handle == &decoder_token); return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_reset(void *handle) { assert(handle == &encoder_token); return ESP_AUDIO_ERR_OK; }
esp_audio_err_t esp_opus_dec_reset(void *handle) { assert(handle == &decoder_token); return ESP_AUDIO_ERR_OK; }
esp_audio_err_t esp_opus_enc_process(void *handle, esp_audio_enc_in_frame_t *in,
                                    esp_audio_enc_out_frame_t *out)
{
    assert(current_task() == named_task("pet_xz_wss") && handle == &encoder_token);
    assert(in->len == 1920 && out->len >= 1500);
    if (is_case("handshake")) {
        const int16_t *pcm = (const int16_t *)in->buffer;
        assert(pcm[0] == 111 && pcm[319] == 111 && pcm[320] == 222 && pcm[639] == 222);
        assert(pcm[640] == 333 && pcm[959] == 333);
    }
    ++encoded;
    memcpy(out->buffer, "\x18\x11\x22", 3);
    out->encoded_bytes = 3;
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_decode(void *handle, esp_audio_dec_in_raw_t *raw,
                                   esp_audio_dec_out_frame_t *frame, esp_audio_dec_info_t *info)
{
    assert(current_task() == named_task("pet_xz_wss") && handle == &decoder_token);
    assert(raw->len == 3 && frame->len == 2880);
    ++decoded;
    if (failing("decode-fail")) return ESP_AUDIO_ERR_INVALID_PARAMETER;
    raw->consumed = raw->len;
    frame->decoded_size = 2880;
    info->sample_rate = 24000; info->bits_per_sample = 16; info->channel = 1;
    int16_t *pcm = (int16_t *)frame->buffer;
    for (unsigned i = 0; i < 1440; ++i) pcm[i] = 2000;
    return ESP_AUDIO_ERR_OK;
}

esp_event_base_t WEBSOCKET_EVENTS = "websocket";
struct esp_websocket_client {
    esp_event_handler_t handler;
    void *context;
    bool live, connected;
};
static struct esp_websocket_client sockets[16];
static char sent_order[128];
static unsigned sent_count;
esp_websocket_client_handle_t esp_websocket_client_init(const esp_websocket_client_config_t *config)
{
    assert(current_task() == named_task("pet_xz_wss") && socket_inits < 16);
    assert(strcmp(config->uri, "wss://voice.example.test/v1/") == 0);
    assert(config->disable_auto_reconnect && !config->enable_close_reconnect);
    assert(!config->skip_cert_common_name_check);
    assert(config->crt_bundle_attach == esp_crt_bundle_attach || config->cert_pem != NULL);
    assert(strstr(config->headers, "Authorization: Bearer test-token\r\n") != NULL);
    assert(strstr(config->headers, "Device-Id: test-device\r\n") != NULL);
    assert(strstr(config->headers, "Client-Id: test-client\r\n") != NULL);
    if (failing("socket-fail") || failing("failure-race")) return NULL;
    struct esp_websocket_client *client = &sockets[socket_inits++];
    *client = (struct esp_websocket_client){.live = true};
    return client;
}
esp_err_t esp_websocket_register_events(esp_websocket_client_handle_t client,
                                       esp_websocket_event_id_t event,
                                       esp_event_handler_t handler, void *context)
{
    assert(client->live && event == WEBSOCKET_EVENT_ANY && handler && context);
    if (failing("register-fail")) return ESP_FAIL;
    client->handler = handler; client->context = context;
    return ESP_OK;
}
esp_err_t esp_websocket_client_start(esp_websocket_client_handle_t client)
{
    assert(client->live && client->handler);
    return failing("socket-start-fail") ? ESP_FAIL : ESP_OK;
}
esp_err_t esp_websocket_client_destroy(esp_websocket_client_handle_t client)
{
    assert(current_task() == named_task("pet_xz_wss") && client->live);
    ++socket_destroys;
    if (failing("destroy-fail")) return ESP_FAIL;
    if (client->handler) client->handler(client->context, WEBSOCKET_EVENTS,
                                        WEBSOCKET_EVENT_DISCONNECTED, NULL);
    client->live = client->connected = false;
    client->handler = NULL;
    return ESP_OK;
}
bool esp_websocket_client_is_connected(esp_websocket_client_handle_t client)
{
    return client != NULL && client->live && client->connected;
}
int esp_websocket_client_send_text(esp_websocket_client_handle_t client, const char *data,
                                   int bytes, TickType_t timeout)
{
    assert(current_task() == named_task("pet_xz_wss") && client->connected && data && bytes > 0);
    assert(timeout > 0 && timeout <= 1000);
    if(is_case("delayed-send")&&data[0]!='{'&&!delayed_write) {
        delayed_write=true;
        atomic_fetch_add(&clock_ms,300);
        if(timeout<300) return 0; /* SDK 的 poll_write 超时，连接应与实机一样失败。 */
    }
    assert(sent_count < sizeof(sent_order));
    sent_order[sent_count++] = data[0] != '{' ? 'B' : strstr(data, "\"hello\"") ? 'H' :
        strstr(data, "\"start\"") ? 'L' : strstr(data, "\"stop\"") ? 'S' : 'A';
    if(data[0]=='{') {
        assert((size_t)bytes<sizeof(last_text));memcpy(last_text,data,(size_t)bytes);last_text[bytes]=0;
    }
    return failing("send-fail") ? bytes - 1 : bytes;
}
int esp_websocket_client_send_bin(esp_websocket_client_handle_t client, const char *data,
                                  int bytes, TickType_t timeout)
{
    return esp_websocket_client_send_text(client, data, bytes, timeout);
}

static void emit(esp_websocket_client_handle_t client, int event, void *data)
{
    assert(client->live && client->handler);
    if (event == WEBSOCKET_EVENT_CONNECTED) client->connected = true;
    if (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_CLOSED)
        client->connected = false;
    client->handler(client->context, WEBSOCKET_EVENTS, event, data);
}
static void fragment(esp_websocket_client_handle_t client, uint8_t opcode, bool fin,
                      const char *data, int bytes, int payload, int offset)
{
    esp_websocket_event_data_t event = {.client = client, .op_code = opcode, .fin = fin,
        .data_ptr = data, .data_len = bytes, .payload_len = payload, .payload_offset = offset};
    emit(client, WEBSOCKET_EVENT_DATA, &event);
}
static void server_hello(esp_websocket_client_handle_t client)
{
    const char hello[] = "{\"type\":\"hello\",\"transport\":\"websocket\",\"session_id\":\"host-session\","
        "\"audio_params\":{\"format\":\"opus\",\"sample_rate\":24000,\"channels\":1,\"frame_duration\":60}}";
    /* 同时覆盖 SDK 将一帧拆成回调、RFC continuation 和控制帧穿插。 */
    fragment(client, 1, false, hello, 17, 64, 0);
    fragment(client, 1, false, hello + 17, 47, 64, 17);
    fragment(client, 10, true, "", 0, 0, 0);
    fragment(client, 0, true, hello + 64, (int)strlen(hello) - 64, (int)strlen(hello) - 64, 0);
}

static bool capturing, playing;
static unsigned capture_starts, playback_starts, written_samples, drained;
static esp_err_t audio_init(void)
{
    assert(current_task() == named_task("pet_ai_agent"));
    return ESP_OK;
}
static esp_err_t audio_deinit(void) { capturing = playing = false; return ESP_OK; }
static esp_err_t capture_start(void)
{
    assert(current_task() == named_task("pet_ai_agent") && !capturing && !playing);
    capturing = true; ++capture_starts;
    if (failing("failure-race")) vTaskDelay(1); /* begin 已接受，A4 尚未发布新轮次。 */
    return ESP_OK;
}
static esp_err_t capture_stop(void) { capturing = false; return ESP_OK; }
static esp_err_t capture_read(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout)
{
    assert(current_task() == named_task("pet_ai_agent") && capturing && !playing && timeout > 0);
    assert(capacity == 320);
    for (unsigned i = 0; i < capacity; ++i) pcm[i] = 2000;
    *samples = capacity;
    atomic_fetch_add(&clock_ms, 20);
    return ESP_OK;
}
static esp_err_t playback_start(uint32_t rate)
{
    assert(current_task() == named_task("pet_ai_agent") && rate == 24000 && !playing && !capturing);
    playing = true; ++playback_starts;
    return ESP_OK;
}
static esp_err_t playback_stop(void) { playing = false; return ESP_OK; }
static esp_err_t playback_write(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout)
{
    assert(current_task() == named_task("pet_ai_agent") && playing && !capturing && timeout > 0);
    for (size_t i = 0; i < samples; ++i) assert(pcm[i] >= 0 && pcm[i] <= 500); /* 末块含 10 ms 淡出。 */
    *written = samples;
    written_samples += (unsigned)samples;
    return ESP_OK;
}
static esp_err_t playback_drain(uint32_t timeout)
{
    assert(current_task() == named_task("pet_ai_agent") && playing && !capturing && timeout > 0);
    ++drained;
    return ESP_OK;
}
static const pet_audio_device_t audio = {
    .id = "host-i2s", .init = audio_init, .deinit = audio_deinit,
    .capture_start = capture_start, .capture_read = capture_read, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = playback_write,
    .playback_stop = playback_stop, .playback_drain = playback_drain,
};
const pet_audio_device_t *pet_bsp_audio(void) { return &audio; }
esp_err_t pet_storage_init(void) { return ESP_OK; }
esp_err_t pet_storage_load_str(const char *space, const char *key, char *out, size_t capacity)
{
    assert(strcmp(space, "xiaozhi") == 0);
    if (failing("boot-missing") && strcmp(key, "ws_token") == 0) return ESP_ERR_NVS_NOT_FOUND;
    if (strcmp(key, "ws_ca") == 0) {
        if (failing("boot-ca-error")) return ESP_ERR_INVALID_SIZE;
        if (failing("boot-empty-ca")) { out[0] = '\0'; return ESP_OK; }
        return ESP_ERR_NVS_NOT_FOUND;
    }
    const char *value = strcmp(key, "ws_uri") == 0 ? "wss://voice.example.test/v1/" :
        strcmp(key, "ws_token") == 0 ? "test-token" :
        strcmp(key, "device_id") == 0 ? "test-device" :
        strcmp(key, "client_id") == 0 ? "test-client" : NULL;
    assert(value != NULL && strlen(value) < capacity);
    memcpy(out, value, strlen(value) + 1);
    return ESP_OK;
}
static pet_ai_agent_snapshot_t ai_status(void)
{
    pet_ai_agent_snapshot_t snapshot;
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK);
    return snapshot;
}
static void tts(esp_websocket_client_handle_t client, const char *state)
{
    char text[128];
    const int bytes = snprintf(text, sizeof(text), "{\"type\":\"tts\",\"state\":\"%s\","
                               "\"session_id\":\"host-session\"}", state);
    fragment(client, 1, true, text, bytes, bytes, 0);
}

int main(int argc, char **argv)
{
    assert(argc <= 2);
    scenario = argc > 1 ? argv[1] : "startup";
    pet_ai_transport_t transport;
    pet_xz_wss_snapshot_t snapshot;
    assert(pet_xz_wss_start(NULL, &transport) == ESP_ERR_INVALID_ARG);
    assert(pet_xz_wss_snapshot(NULL) == ESP_ERR_INVALID_ARG);
    assert(pet_xz_wss_snapshot(&snapshot) == ESP_ERR_INVALID_STATE);
    char uri_input[64] = "wss://voice.example.test/v1/", token_input[32] = "test-token";
    pet_xz_wss_config_t config = {
        .uri = uri_input, .token = token_input,
        .device_id = "test-device", .client_id = "test-client",
    };
    assert(pet_xz_wss_start(&config, NULL) == ESP_ERR_INVALID_ARG);
    const char *invalid_uris[] = {"ws://voice.test/", "wss:///missing-host", "wss://host:0/",
        "wss://host:65536/", "wss://user:pass@host/", "wss://host/#fragment", "wss://host/\r\nX:1"};
    for (size_t i = 0; i < sizeof(invalid_uris) / sizeof(invalid_uris[0]); ++i) {
        pet_xz_wss_config_t invalid = config;
        invalid.uri = invalid_uris[i];
        assert(pet_xz_wss_start(&invalid, &transport) == ESP_ERR_INVALID_ARG);
    }
    const char *invalid_tokens[] = {NULL, "", "Bearer ", " ", "raw token", "token\r\nX:1"};
    for (size_t i = 0; i < sizeof(invalid_tokens) / sizeof(invalid_tokens[0]); ++i) {
        pet_xz_wss_config_t invalid = config;
        invalid.token = invalid_tokens[i];
        assert(pet_xz_wss_start(&invalid, &transport) == ESP_ERR_INVALID_ARG);
    }
    assert(allocations == 0 && codec_opens == 0 && socket_inits == 0);
    if (is_case("boot-missing") || is_case("boot-ca-error") || is_case("boot-empty-ca") ||
        is_case("boot-start")) {
        if (!is_case("boot-start")) {
            const esp_err_t expected = is_case("boot-missing") ? ESP_ERR_NVS_NOT_FOUND :
                is_case("boot-empty-ca") ? ESP_ERR_INVALID_ARG : ESP_ERR_INVALID_SIZE;
            assert(app_ai_start(25, 25, false, false, pet_storage_load_str,NULL) == expected);
            assert(task_count == 0 && socket_inits == 0);
            assert(pet_xz_wss_snapshot(&snapshot) == ESP_ERR_INVALID_STATE);
            failure_armed = false;
        }
        assert(app_ai_start(25, 25, false, false, pet_storage_load_str,NULL) == ESP_OK);
        step("pet_xz_wss");
        step("pet_ai_agent");
        assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.ready);
        assert(ai_status().ready && ai_status().view.state == PET_AI_IDLE);
        assert(capture_starts == 0 && socket_inits == 0);
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        step("pet_ai_agent");
        step("pet_xz_wss");
        assert(socket_inits == 1 && capturing);
        puts("AI startup: strict NVS reads, opt-in A4/WSS composition and explicit talk trigger passed");
        return 0;
    }
    if (is_case("alloc-fail") || is_case("tx-queue-fail") || is_case("rx-queue-fail") || is_case("task-fail")) {
        assert(pet_xz_wss_start(&config, &transport) == ESP_ERR_NO_MEM);
        assert(allocations == 0 && queues == 0 && task_count == 0 && codec_opens == 0);
        failure_armed = false;
    }
    if(is_case("app-tools")) assert(app_tools_start()==ESP_OK);
    if(is_case("tools")) {
        static const pet_intent_tool_t tools[]={{"self.count","test counter",
            "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}",true,count_tool,NULL}};
        assert(pet_intent_init(&intents,tools,1));
        assert(pet_xz_wss_set_tools(tool_handler,&intents)==ESP_OK);
    }
    assert(pet_xz_wss_start(&config, &transport) == ESP_OK);
    assert(transport.begin_turn && transport.forward_pcm && transport.end_input && transport.cancel_turn);
    assert(codec_opens == 0 && socket_inits == 0);
    assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && !snapshot.ready);
    assert(transport.begin_turn(transport.context, 1) != PET_AI_PORT_OK);
    step("pet_xz_wss");
    assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK);
    if (is_case("codec-fail")) {
        assert(snapshot.faulted && !snapshot.ready && snapshot.last_error == ESP_ERR_NO_MEM);
        assert(socket_inits == 0);
        puts("Xiaozhi WSS: asynchronous codec failure never opens a socket passed");
        return 0;
    }
    assert(snapshot.ready && snapshot.network_ready && !snapshot.faulted && codec_opens == 1);
    assert(socket_inits == 0);
    assert(pet_xz_wss_start(&config, &transport) == ESP_ERR_INVALID_STATE);
    if(is_case("tools")) {
        assert(pet_xz_wss_set_tools(NULL,NULL)==ESP_ERR_INVALID_STATE);
        assert(transport.begin_turn(transport.context,1)==PET_AI_PORT_OK);
        step("pet_xz_wss");emit(&sockets[0],WEBSOCKET_EVENT_CONNECTED,NULL);step("pet_xz_wss");
        assert(strstr(last_text,"\"mcp\":true"));
        server_hello(&sockets[0]);for(unsigned i=0;i<4;++i)step("pet_xz_wss");
        const char request[]="{\"type\":\"mcp\",\"session_id\":\"host-session\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"self.count\",\"arguments\":{}}}}";
        for(unsigned attempt=0;attempt<2;++attempt) {
            fragment(&sockets[0],1,true,request,sizeof(request)-1,sizeof(request)-1,0);step("pet_xz_wss");
            cJSON *response=cJSON_Parse(last_text),*payload=cJSON_GetObjectItemCaseSensitive(response,"payload");
            assert(cJSON_IsString(cJSON_GetObjectItemCaseSensitive(response,"type"))&&
                !strcmp(cJSON_GetObjectItemCaseSensitive(response,"type")->valuestring,"mcp"));
            assert(!strcmp(cJSON_GetObjectItemCaseSensitive(response,"session_id")->valuestring,"host-session"));
            assert(cJSON_GetObjectItemCaseSensitive(payload,"id")->valueint==2&&cJSON_GetObjectItemCaseSensitive(payload,"result"));
            cJSON_Delete(response);assert(tool_executions==1);
        }
        unsigned sent_before=sent_count;
        const char notification[]="{\"type\":\"mcp\",\"session_id\":\"host-session\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}}";
        fragment(&sockets[0],1,true,notification,sizeof(notification)-1,sizeof(notification)-1,0);step("pet_xz_wss");
        assert(sent_count==sent_before&&tool_executions==1);
        assert(pet_xz_wss_snapshot(&snapshot)==ESP_OK&&snapshot.mcp_received==3&&
            snapshot.mcp_sent==2&&snapshot.mcp_failed==0);
        assert(transport.cancel_turn(transport.context,1)==PET_AI_PORT_OK);step("pet_xz_wss");
        assert(!sockets[0].live&&socket_destroys==1);
        puts("Xiaozhi WSS: MCP advertisement, real tool RPC envelope, duplicate action and notification passed");
        return 0;
    }
    if (is_case("network-gate")) {
        wifi = false;
        step("pet_xz_wss");
        assert(transport.begin_turn(transport.context, 1) != PET_AI_PORT_OK);
        wifi = true; synced = false;
        step("pet_xz_wss");
        assert(transport.begin_turn(transport.context, 1) != PET_AI_PORT_OK);
        assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && !snapshot.network_ready && socket_inits == 0);
        synced = true;
        step("pet_xz_wss");
        assert(transport.begin_turn(transport.context, 1) == PET_AI_PORT_OK);
        step("pet_xz_wss");
        assert(socket_inits == 1);
        puts("Xiaozhi WSS: missing IP or SNTP refuses a turn without requiring MQTT online passed");
        return 0;
    }
    if (is_case("socket-fail") || is_case("register-fail") || is_case("socket-start-fail") ||
        is_case("send-fail") || is_case("handshake-timeout") || is_case("failure-race") ||
        is_case("rx-overflow") || is_case("malformed-offset") || is_case("assembly-timeout") ||
        is_case("remote-close") || is_case("network-lost") || is_case("failure-backpressure")) {
        const pet_ai_agent_config_t ai = {.audio = &audio, .transport = transport, .volume_pct = 25};
        assert(pet_ai_agent_start(&ai) == ESP_OK);
        step("pet_ai_agent");
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        step("pet_ai_agent");
        if (is_case("failure-race")) assert(ai_status().view.turn_id == 0 && capturing);
        step("pet_xz_wss");
        if (is_case("send-fail")) {
            emit(&sockets[0], WEBSOCKET_EVENT_CONNECTED, NULL);
            step("pet_xz_wss");
        } else if (is_case("handshake-timeout")) {
            atomic_fetch_add(&clock_ms, 5000);
            step("pet_xz_wss");
        } else if (is_case("remote-close")) {
            emit(&sockets[0], WEBSOCKET_EVENT_DISCONNECTED, NULL);
            step("pet_xz_wss");
        } else if (is_case("network-lost")) {
            wifi = false;
            step("pet_xz_wss");
        } else if (is_case("failure-backpressure")) {
            for (unsigned i = 0; i < 8; ++i)
                assert(pet_ai_agent_post(PET_AI_RESPONSE_BEGIN, 99) == ESP_OK);
            emit(&sockets[0], WEBSOCKET_EVENT_ERROR, NULL);
            step("pet_xz_wss");
        } else if (is_case("rx-overflow") || is_case("malformed-offset") || is_case("assembly-timeout")) {
            emit(&sockets[0], WEBSOCKET_EVENT_CONNECTED, NULL);
            step("pet_xz_wss");
            if (is_case("rx-overflow")) {
                for (unsigned i = 0; i < 40; ++i) fragment(&sockets[0], 1, true, "{", 1, 1, 0);
            } else if (is_case("malformed-offset")) {
                fragment(&sockets[0], 1, true, "{}", 2, 3, 1);
            } else {
                fragment(&sockets[0], 1, true, "{\"type\"", 7, 64, 0);
                step("pet_xz_wss");
                atomic_fetch_add(&clock_ms, 2000);
            }
            step("pet_xz_wss");
        }
        step("pet_xz_wss");
        if (is_case("failure-race")) {
            step("pet_ai_agent");
            step("pet_xz_wss");
        }
        step("pet_ai_agent");
        if (is_case("failure-backpressure")) {
            assert(ai_status().view.state == PET_AI_LISTENING);
            for (unsigned i = 0; i < 9; ++i) { step("pet_xz_wss"); step("pet_ai_agent"); }
        }
        assert(ai_status().view.state == PET_AI_ERROR && ai_status().view.reason == PET_AI_REASON_TRANSPORT_ERROR);
        assert(ai_status().ready && !ai_status().faulted && !capturing && drained == 0);
        assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.last_error != ESP_OK && !snapshot.faulted);
        const unsigned before = socket_inits;
        step("pet_xz_wss");
        assert(socket_inits == before); /* 失败不自动重连、不重放输入。 */
        failure_armed = false;
        wifi = synced = true;
        step("pet_xz_wss");
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        step("pet_ai_agent");
        step("pet_xz_wss");
        assert(ai_status().view.turn_id == 2 && ai_status().view.state == PET_AI_LISTENING);
        assert(socket_inits == before + 1);
        puts("Xiaozhi WSS: connection/handshake/send failure reaches A4 and only explicit retry starts a fresh turn passed");
        return 0;
    }
    if (is_case("queue-cancel")) {
        int16_t pcm[320] = {1000};
        assert(transport.begin_turn(transport.context, 1) == PET_AI_PORT_OK);
        unsigned accepted = 0;
        while (transport.forward_pcm(transport.context, 1, pcm, 320) == PET_AI_PORT_OK) {
            assert(++accepted < 1000);
        }
        assert(accepted > 0);
        assert(transport.cancel_turn(transport.context, 1) == PET_AI_PORT_OK);
        assert(transport.begin_turn(transport.context, 2) == PET_AI_PORT_OK);
        assert(transport.forward_pcm(transport.context, 2, pcm, 320) == PET_AI_PORT_OK);
        assert(transport.forward_pcm(transport.context, 1, pcm, 320) != PET_AI_PORT_OK);
        step("pet_xz_wss");
        assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.turn_id == 2);
        assert(snapshot.rejected_frames == 1 && encoded == 0 && socket_inits == 1);
        puts("Xiaozhi WSS: full PCM queue cannot block cancellation or a fresh turn passed");
        return 0;
    }
    if (is_case("handshake")||is_case("delayed-send")) {
        strcpy(uri_input, "wss://changed.example.test/");
        strcpy(token_input, "changed-token");
        assert(transport.begin_turn(transport.context, 1) == PET_AI_PORT_OK);
        int16_t pcm[320];
        for (unsigned frame = 1; frame <= 3; ++frame) {
            for (unsigned i = 0; i < 320; ++i) pcm[i] = (int16_t)(frame * 111);
            assert(transport.forward_pcm(transport.context, 1, pcm, 320) == PET_AI_PORT_OK);
        }
        memset(pcm, 0, sizeof(pcm)); /* 入队后生产者立即复用原缓冲区。 */
        assert(transport.end_input(transport.context, 1) == PET_AI_PORT_OK);
        step("pet_xz_wss");
        assert(socket_inits == 1 && sent_count == 0 && encoded == 0);
        emit(&sockets[0], WEBSOCKET_EVENT_CONNECTED, NULL);
        step("pet_xz_wss");
        assert(sent_count == 1 && sent_order[0] == 'H' && encoded == 0);
        server_hello(&sockets[0]);
        for (unsigned i = 0; i < 8; ++i) step("pet_xz_wss");
        assert(sent_count == 4 && memcmp(sent_order, "HLBS", 4) == 0 && encoded == 1);
        assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.turn_id == 1 && snapshot.epoch == 1);
        puts("Xiaozhi WSS: TLS configuration, queued PCM ownership, fragmented hello and ordered input passed");
        return 0;
    }
    if (is_case("playback") || is_case("barge-in") || is_case("destroy-fail") ||
        is_case("graceful-close") || is_case("playback-stall") || is_case("decode-fail") ||
        is_case("first-audio-delay") || is_case("first-audio-timeout") ||
        is_case("privacy") || is_case("app-tools")) {
        const pet_ai_agent_config_t ai = {.audio = &audio, .transport = transport, .volume_pct = 25};
        assert(pet_ai_agent_start(&ai) == ESP_OK);
        step("pet_ai_agent");
        assert(ai_status().ready && capture_starts == 0 && socket_inits == 0);
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        step("pet_ai_agent");
        step("pet_xz_wss");
        assert(socket_inits == 1 && capturing && encoded == 0);
        emit(&sockets[0], WEBSOCKET_EVENT_CONNECTED, NULL);
        step("pet_xz_wss");
        server_hello(&sockets[0]);
        for (unsigned i = 1; i < 15; ++i) { step("pet_ai_agent"); step("pet_xz_wss"); }
        assert(pet_ai_agent_post(PET_AI_TALK_RELEASE, 1) == ESP_OK);
        step("pet_ai_agent");
        step("pet_xz_wss");
        assert(ai_status().view.state == PET_AI_THINKING && !capturing && !playing);
        assert(encoded == 5 && sent_order[sent_count - 1] == 'S');
        if (is_case("barge-in") || is_case("destroy-fail")) {
            tts(&sockets[0], "start");
            fragment(&sockets[0], 2, true, "\x18\x11\x22", 3, 3, 0);
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
            step("pet_ai_agent");
            assert(ai_status().view.turn_id == 2 && capturing);
            const unsigned captured_before_close = ai_status().captured_frames;
            step("pet_xz_wss");
            if (is_case("destroy-fail")) {
                assert(socket_destroys == 1 && sockets[0].live && decoded == 0);
                assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.faulted && !snapshot.ready);
                step("pet_xz_wss");
                step("pet_ai_agent");
                assert(ai_status().view.state == PET_AI_ERROR && !capturing && !ai_status().faulted);
                assert(ai_status().captured_frames == captured_before_close);
                assert(socket_inits == 1 && socket_destroys == 1);
                assert(transport.begin_turn(transport.context, 3) != PET_AI_PORT_OK);
                assert(transport.cancel_turn(transport.context, 2) == PET_AI_PORT_OK);
                puts("Xiaozhi WSS: uncertain destroy fails the new turn promptly and prevents socket reuse passed");
                return 0;
            }
            assert(socket_destroys == 1 && !sockets[0].live && decoded == 0);
            step("pet_xz_wss");
            assert(socket_inits == 2 && sockets[1].live);
            emit(&sockets[1], WEBSOCKET_EVENT_CONNECTED, NULL);
            step("pet_xz_wss");
            server_hello(&sockets[1]);
            step("pet_xz_wss");
            for (unsigned i = 0; i < 2; ++i) { step("pet_ai_agent"); step("pet_xz_wss"); }
            assert(encoded == 6 && decoded == 0 && playback_starts == 0);
            assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.turn_id == 2 && snapshot.epoch == 2);
            puts("Xiaozhi WSS: joined old connection, discarded old response and preserved new first PCM passed");
            return 0;
        }
        if(is_case("app-tools")) {
            assert(pet_ai_agent_text(1,PET_AI_TEXT_USER,"上下文测试问题")==ESP_OK);
            assert(pet_ai_agent_text(1,PET_AI_TEXT_ASSISTANT,"上下文测试回答")==ESP_OK);
        }
        tts(&sockets[0], "start");
        if(is_case("first-audio-delay")||is_case("first-audio-timeout")) {
            step("pet_xz_wss");step("pet_ai_agent");
            atomic_fetch_add(&clock_ms,is_case("first-audio-timeout")?PET_AI_RESPONSE_TIMEOUT_MS:1200);
            step("pet_ai_agent");step("pet_xz_wss");
            if(is_case("first-audio-timeout")) {
                assert(ai_status().view.state==PET_AI_ERROR&&ai_status().view.reason==PET_AI_REASON_RESPONSE_TIMEOUT);
                assert(!playing&&!capturing&&decoded==0&&drained==0&&socket_destroys==1);
                puts("Xiaozhi WSS: synthesis without first audio retains the original response deadline passed");
                return 0;
            }
            assert(ai_status().view.state==PET_AI_THINKING&&playback_starts==0&&socket_destroys==0);
        }
        for (unsigned i = 0; i < 3; ++i)
            fragment(&sockets[0], 2, true, "\x18\x11\x22", 3, 3, 0);
        tts(&sockets[0], "stop");
        if (is_case("graceful-close")) emit(&sockets[0], WEBSOCKET_EVENT_CLOSED, NULL);
        if (is_case("decode-fail")) {
            step("pet_xz_wss");
            step("pet_xz_wss");
            for (unsigned i = 0; i < 3; ++i) step("pet_ai_agent");
            assert(ai_status().view.state == PET_AI_ERROR && !ai_status().faulted && !playing);
            assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.faulted && !snapshot.ready);
            assert(decoded == 1 && written_samples == 0 && drained == 0 && socket_destroys == 1);
            assert(transport.begin_turn(transport.context, 2) != PET_AI_PORT_OK);
            puts("Xiaozhi WSS: codec failure stops the scoped turn and prevents reuse passed");
            return 0;
        }
        for (unsigned i = 0; i < 4; ++i) step("pet_xz_wss");
        assert(ai_status().view.state == PET_AI_THINKING && playback_starts == 0);
        assert(decoded == 3 && drained == 0); /* 160 ms 队列满，第三包末块保留，未提交结束标记。 */
        if (is_case("privacy")) {
            assert(pet_ai_agent_set_dnd(true) == ESP_OK);
            step("pet_ai_agent");
            step("pet_xz_wss");
            assert(ai_status().view.state == PET_AI_MUTED && !capturing && !playing);
            assert(socket_destroys == 1 && !sockets[0].live && written_samples == 0 && drained == 0);
            assert(pet_ai_agent_ptt(true) == ESP_ERR_INVALID_STATE);
            assert(pet_ai_agent_set_dnd(false) == ESP_OK);
            step("pet_ai_agent");
            step("pet_xz_wss");
            assert(ai_status().view.state == PET_AI_IDLE && socket_inits == 1);
            assert(pet_ai_agent_ptt(true) == ESP_OK);
            step("pet_ai_agent");
            step("pet_xz_wss");
            assert(ai_status().view.turn_id == 2 && capturing && socket_inits == 2);
            assert(decoded == 3 && written_samples == 0);
            puts("Xiaozhi WSS: privacy cancels queued response, closes socket and requires fresh PTT passed");
            return 0;
        }
        if (is_case("playback-stall")) {
            atomic_fetch_add(&clock_ms, 500);
            step("pet_xz_wss");
            step("pet_xz_wss");
            for (unsigned i = 0; i < 3; ++i) step("pet_ai_agent");
            assert(ai_status().view.state == PET_AI_ERROR && !playing && drained == 0);
            assert(pet_xz_wss_snapshot(&snapshot) == ESP_OK && snapshot.last_error == ESP_ERR_TIMEOUT);
            assert(!snapshot.faulted && decoded == 3 && socket_destroys == 1);
            puts("Xiaozhi WSS: stalled A4 output fails within the software budget without a false drain passed");
            return 0;
        }
        for (unsigned i = 0; i < 14; ++i) { step("pet_ai_agent"); step("pet_xz_wss"); }
        assert(ai_status().view.state == PET_AI_IDLE && ai_status().view.reason == PET_AI_REASON_DRAINED);
        assert(decoded == 3 && written_samples == 4320 && drained == 1 && !playing);
        assert(socket_destroys == 1 && !sockets[0].live);
        if(is_case("app-tools")) {
            /* A completed real A4 turn, then a new WSS connection reading the production registry. */
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS,0)==ESP_OK);step("pet_ai_agent");step("pet_xz_wss");
            emit(&sockets[1],WEBSOCKET_EVENT_CONNECTED,NULL);step("pet_xz_wss");
            assert(strstr(last_text,"\"mcp\":true"));
            server_hello(&sockets[1]);for(unsigned i=0;i<4;++i)step("pet_xz_wss");
            const char list[]="{\"type\":\"mcp\",\"session_id\":\"host-session\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/list\"}}";
            fragment(&sockets[1],1,true,list,sizeof(list)-1,sizeof(list)-1,0);step("pet_xz_wss");
            assert(strstr(last_text,"self.get_context")&&strstr(last_text,"self.get_unread")&&!strstr(last_text,"self.servo"));
            const char get_context[]="{\"type\":\"mcp\",\"session_id\":\"host-session\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"self.get_context\",\"arguments\":{}}}}";
            for(unsigned attempt=0;attempt<2;++attempt) {
                fragment(&sockets[1],1,true,get_context,sizeof(get_context)-1,sizeof(get_context)-1,0);step("pet_xz_wss");
                assert(strstr(last_text,"上下文测试问题")&&strstr(last_text,"上下文测试回答")&&strstr(last_text,"豆豆"));
                assert(!strstr(last_text,"test-token")&&!strstr(last_text,"voice.example.test"));
            }
            const char denied[]="{\"type\":\"mcp\",\"session_id\":\"host-session\",\"payload\":{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"self.get_context\",\"arguments\":{\"rawAudio\":true}}}}";
            fragment(&sockets[1],1,true,denied,sizeof(denied)-1,sizeof(denied)-1,0);step("pet_xz_wss");
            assert(strstr(last_text,"-32602"));
            assert(pet_ai_agent_forget()==ESP_OK);
            unsigned before_forget=sent_count;
            fragment(&sockets[1],1,true,get_context,sizeof(get_context)-1,sizeof(get_context)-1,0);step("pet_xz_wss");
            assert(sent_count==before_forget); /* Pending privacy cancellation sends no stale or new context. */
            step("pet_ai_agent");step("pet_xz_wss");
            assert(socket_destroys==2&&!sockets[1].live&&!capturing);
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS,0)==ESP_OK);step("pet_ai_agent");step("pet_xz_wss");
            emit(&sockets[2],WEBSOCKET_EVENT_CONNECTED,NULL);step("pet_xz_wss");
            server_hello(&sockets[2]);for(unsigned i=0;i<4;++i)step("pet_xz_wss");
            fragment(&sockets[2],1,true,get_context,sizeof(get_context)-1,sizeof(get_context)-1,0);step("pet_xz_wss");
            assert(strstr(last_text,"history")&&!strstr(last_text,"上下文测试问题")&&!strstr(last_text,"上下文测试回答"));
            assert(pet_ai_agent_set_dnd(true)==ESP_OK);step("pet_ai_agent");step("pet_xz_wss");
            assert(socket_destroys==3&&!sockets[2].live&&!capturing);
            assert(pet_xz_wss_snapshot(&snapshot)==ESP_OK&&snapshot.mcp_received==6&&
                snapshot.mcp_sent==5&&snapshot.mcp_failed==1);
            puts("Xiaozhi WSS: production registry, completed-turn context, privacy cancellation, fresh-context reload and strict arguments passed");
            return 0;
        }
        puts("Xiaozhi WSS: real A4 capture/response queues, lossless backpressure and separate DMA drain passed");
        return 0;
    }
    puts("Xiaozhi WSS: bounded startup, asynchronous initialization and no automatic connection passed");
    return 0;
}
