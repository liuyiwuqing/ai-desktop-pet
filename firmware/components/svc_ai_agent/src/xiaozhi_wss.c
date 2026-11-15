#include "svc_ai_agent/wss.h"

#include <stdio.h>
#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "http_parser.h"
#include "svc_ai_agent/opus.h"
#include "svc_net/api.h"
#include "cJSON.h"

#define TX_DEPTH 128u
#define RX_DEPTH 32u
#define RX_CHUNK_BYTES 512u
#define HANDSHAKE_MS 5000u
#define ASSEMBLY_MS 2000u
#define PLAYBACK_BLOCK_MS 500u
/* 公网首轮会有短暂写背压；失败仍关闭本轮，绝不重放已编码音频。 */
#define SEND_MS 1000u
/* SDK Opus 在当前拥有者同步编解码；vendor 的预算不含在堆统计中。
 * 按已锁定 esp_audio_codec 的 40 KiB 栈建议预留，并通过实机水位核验。 */
#define WORKER_STACK_BYTES (40u * 1024u)

typedef struct wss_owner wss_owner_t;
typedef struct {
    wss_owner_t *owner;
    uint32_t epoch;
} connection_binding_t;

typedef struct {
    uint32_t turn_id;
    bool end;
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES];
} tx_item_t;

typedef struct {
    uint32_t epoch;
    int length, payload_length, offset;
    uint8_t opcode;
    bool fin;
    char data[RX_CHUNK_BYTES];
} rx_item_t;

struct wss_owner {
    char uri[PET_XZ_WSS_URI_BYTES + 1];
    char headers[PET_XZ_WSS_TOKEN_BYTES + 2 * PET_XZ_WSS_ID_BYTES + 128];
    char ca[PET_XZ_WSS_CA_BYTES + 1];
    QueueHandle_t tx, rx;
    pet_opus_t opus;
    pet_xz_t protocol;
    pet_xz_wss_snapshot_t status;
    uint32_t requested_turn, cancelled_through;
    bool input_ended;
    esp_websocket_client_handle_t socket;
    connection_binding_t binding;
    bool callbacks_enabled, connected, disconnected;
    esp_err_t rx_error;
    uint32_t active_turn, completed_turn, failure_turn;
    uint64_t started_ms, assembly_ms, blocked_ms;
    bool hello_sent, response_pending, tx_pending;
    tx_item_t outgoing;
    uint8_t packet[PET_XZ_PACKET_BYTES];
    char control[PET_XZ_CONTROL_BYTES];
    char *tool_response;
    /* 一个完整应用消息；SDK 分块和 RFC continuation 都只能顺序追加。 */
    uint8_t message[PET_XZ_PACKET_BYTES];
    size_t message_bytes, frame_bytes, frame_expected;
    uint8_t message_opcode, frame_opcode;
    bool message_active, message_ready, frame_fin;
};

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static wss_owner_t *service;
static pet_xz_tool_handler_t tools_handler;
static void *tools_context;
static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static bool bounded_text(const char *value, size_t maximum, bool space)
{
    if (value == NULL || value[0] == '\0') return false;
    for (size_t i = 0; i <= maximum; ++i) {
        const unsigned char ch = (unsigned char)value[i];
        if (ch == 0) return i <= maximum;
        if (ch < (space ? 32 : 33) || ch > 126) return false;
    }
    return false;
}

static bool valid_config(const pet_xz_wss_config_t *config)
{
    if (config == NULL || !bounded_text(config->uri, PET_XZ_WSS_URI_BYTES, false) ||
        strncmp(config->uri, "wss://", 6) != 0 || config->uri[6] == '\0' ||
        strchr(config->uri, '@') || strchr(config->uri, '#') ||
        !bounded_text(config->token, PET_XZ_WSS_TOKEN_BYTES, true) ||
        !bounded_text(config->device_id, PET_XZ_WSS_ID_BYTES, false) ||
        !bounded_text(config->client_id, PET_XZ_WSS_ID_BYTES, false)) return false;
    const char *token = strncmp(config->token, "Bearer ", 7) == 0 ? config->token + 7 : config->token;
    if (!bounded_text(token, PET_XZ_WSS_TOKEN_BYTES, false)) return false;
    struct http_parser_url uri;
    http_parser_url_init(&uri);
    if (http_parser_parse_url(config->uri, strlen(config->uri), 0, &uri) != 0 ||
        uri.field_data[UF_HOST].len == 0 || (uri.field_set & (1u << UF_USERINFO)) != 0 ||
        (uri.field_set & (1u << UF_FRAGMENT)) != 0 ||
        ((uri.field_set & (1u << UF_PORT)) != 0 && uri.port == 0)) return false;
    if (config->ca_cert_pem != NULL) {
        size_t length = 0;
        while (length <= PET_XZ_WSS_CA_BYTES && config->ca_cert_pem[length] != '\0') ++length;
        if (length == 0 || length > PET_XZ_WSS_CA_BYTES) return false;
    }
    return true;
}

static bool network_ready(void)
{
    if (!pet_net_is_time_synced()) return false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t info = {0};
    return netif != NULL && esp_netif_is_netif_up(netif) &&
        esp_netif_get_ip_info(netif, &info) == ESP_OK && info.ip.addr != 0;
}

static pet_ai_port_result_t begin_turn(void *context, uint32_t turn)
{
    wss_owner_t *owner = context;
    if (owner == NULL || turn == 0) return PET_AI_PORT_INVALID;
    portENTER_CRITICAL(&status_lock);
    const bool accepted = owner->status.ready && owner->status.network_ready &&
        !owner->status.faulted && turn > owner->requested_turn && turn > owner->cancelled_through;
    if (accepted) {
        owner->requested_turn = turn;
        owner->input_ended = false;
    }
    portEXIT_CRITICAL(&status_lock);
    /* 端口只有一个 A4 生产者；begin 返回前还没有新轮次 PCM。先清掉旧积压，
     * 防止旧轮次的满队列把新轮次第一帧也挤掉。网络拥有者持有的项仍按 turn 丢弃。 */
    if (accepted) xQueueReset(owner->tx);
    return accepted ? PET_AI_PORT_OK : PET_AI_PORT_FAILED;
}

static pet_ai_port_result_t enqueue(wss_owner_t *owner, const tx_item_t *item)
{
    if (owner == NULL || item->turn_id == 0) return PET_AI_PORT_INVALID;
    portENTER_CRITICAL(&status_lock);
    const bool accepted = owner->status.ready && !owner->status.faulted &&
        item->turn_id == owner->requested_turn && item->turn_id > owner->cancelled_through &&
        !owner->input_ended;
    portEXIT_CRITICAL(&status_lock);
    if (!accepted) return PET_AI_PORT_FAILED;
    if (xQueueSend(owner->tx, item, 0) != pdTRUE) {
        portENTER_CRITICAL(&status_lock);
        ++owner->status.rejected_frames;
        portEXIT_CRITICAL(&status_lock);
        return PET_AI_PORT_FAILED;
    }
    if (item->end) {
        portENTER_CRITICAL(&status_lock);
        owner->input_ended = true;
        portEXIT_CRITICAL(&status_lock);
    }
    return PET_AI_PORT_OK;
}

static pet_ai_port_result_t forward_pcm(void *context, uint32_t turn,
                                        const int16_t *pcm, size_t samples)
{
    if (pcm == NULL || samples != PET_AUDIO_CAPTURE_SAMPLES) return PET_AI_PORT_INVALID;
    tx_item_t item = {.turn_id = turn};
    memcpy(item.pcm, pcm, sizeof(item.pcm));
    return enqueue(context, &item);
}

static pet_ai_port_result_t end_input(void *context, uint32_t turn)
{
    const tx_item_t item = {.turn_id = turn, .end = true};
    return enqueue(context, &item);
}

static pet_ai_port_result_t cancel_turn(void *context, uint32_t turn)
{
    wss_owner_t *owner = context;
    if (owner == NULL || turn == 0) return PET_AI_PORT_INVALID;
    /* 独立于 PCM 队列；接受取消不等于网络已经关闭。 */
    portENTER_CRITICAL(&status_lock);
    if (turn > owner->cancelled_through) owner->cancelled_through = turn;
    portEXIT_CRITICAL(&status_lock);
    return PET_AI_PORT_OK;
}

static void on_socket(void *context, esp_event_base_t base, int32_t event, void *data)
{
    const connection_binding_t *binding = context;
    wss_owner_t *owner = binding->owner;
    if (base != WEBSOCKET_EVENTS) return;
    portENTER_CRITICAL(&status_lock);
    const bool accepted = owner->callbacks_enabled && owner->binding.epoch == binding->epoch;
    if (!accepted) ++owner->status.stale_items;
    if (accepted && event == WEBSOCKET_EVENT_CONNECTED) owner->connected = true;
    if (accepted && (event == WEBSOCKET_EVENT_DISCONNECTED || event == WEBSOCKET_EVENT_CLOSED))
        owner->disconnected = true;
    if (accepted && event == WEBSOCKET_EVENT_ERROR) owner->rx_error = ESP_FAIL;
    portEXIT_CRITICAL(&status_lock);
    if (!accepted || event != WEBSOCKET_EVENT_DATA) return;
    const esp_websocket_event_data_t *input = data;
    if (input == NULL || input->data_len < 0 || input->data_len > (int)RX_CHUNK_BYTES ||
        (input->data_len > 0 && input->data_ptr == NULL)) {
        portENTER_CRITICAL(&status_lock);
        owner->rx_error = ESP_ERR_INVALID_SIZE;
        portEXIT_CRITICAL(&status_lock);
        return;
    }
    /* SDK 自行处理 PING/PONG/CLOSE；它们不打断应用消息的 continuation。 */
    if (input->op_code == 8 || input->op_code == 9 || input->op_code == 10) return;
    rx_item_t item = {.epoch = binding->epoch, .length = input->data_len,
        .payload_length = input->payload_len, .offset = input->payload_offset,
        .opcode = input->op_code, .fin = input->fin};
    if (item.length > 0) memcpy(item.data, input->data_ptr, (size_t)item.length);
    if (xQueueSend(owner->rx, &item, 0) != pdTRUE) {
        portENTER_CRITICAL(&status_lock);
        owner->rx_error = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&status_lock);
    }
}

static esp_err_t send_data(wss_owner_t *owner, const void *data, size_t bytes, bool binary)
{
    if (bytes == 0) return ESP_OK;
    if (!esp_websocket_client_is_connected(owner->socket)) return ESP_FAIL;
    const TickType_t ticks = pdMS_TO_TICKS(SEND_MS);
    const int sent = binary ? esp_websocket_client_send_bin(owner->socket, data, (int)bytes,
                                                           ticks > 0 ? ticks : 1) :
        esp_websocket_client_send_text(owner->socket, data, (int)bytes, ticks > 0 ? ticks : 1);
    if (sent != (int)bytes) return ESP_FAIL; /* 不能重放已经消费的 PCM。 */
    if (binary) {
        portENTER_CRITICAL(&status_lock);
        ++owner->status.sent_packets;
        portEXIT_CRITICAL(&status_lock);
    }
    return ESP_OK;
}

static void clear_message(wss_owner_t *owner)
{
    memset(owner->message, 0, sizeof(owner->message));
    owner->message_bytes = owner->frame_bytes = owner->frame_expected = 0;
    owner->message_active = owner->message_ready = false;
    owner->assembly_ms = 0;
}

static bool close_turn(wss_owner_t *owner, bool abort)
{
    if (abort && owner->socket != NULL && esp_websocket_client_is_connected(owner->socket)) {
        size_t bytes = 0;
        if (pet_xz_abort(&owner->protocol, owner->binding.epoch,
                          owner->control, sizeof(owner->control), &bytes) == PET_XZ_OK)
            (void)send_data(owner, owner->control, bytes, false);
    }
    portENTER_CRITICAL(&status_lock);
    owner->callbacks_enabled = false;
    portEXIT_CRITICAL(&status_lock);
    /* 锁定 SDK 的 destroy 会停止并等待旧任务、删除事件循环后才释放资源。
     * 返回前不复用 binding，不创建新连接；不能在 SDK 回调里执行。 */
    const esp_err_t cleanup = owner->socket == NULL ? ESP_OK :
        esp_websocket_client_destroy(owner->socket);
    if (cleanup != ESP_OK) {
        portENTER_CRITICAL(&status_lock);
        owner->status.cleanup_error = cleanup;
        owner->status.last_error = cleanup;
        owner->status.faulted = true;
        owner->status.ready = false;
        /* 关闭旧连接时可能已经接受新 begin；新轮次同样无法继续，立即回送失败。 */
        owner->failure_turn = owner->requested_turn;
        portEXIT_CRITICAL(&status_lock);
        return false; /* 保留不确定句柄/context，重启前不再销毁或复用。 */
    }
    owner->socket = NULL;
    if (owner->binding.epoch != 0) (void)pet_xz_close(&owner->protocol, owner->binding.epoch);
    owner->completed_turn = owner->active_turn;
    owner->active_turn = 0;
    owner->hello_sent = owner->response_pending = false;
    owner->blocked_ms = 0;
    clear_message(owner);
    memset(owner->packet, 0, sizeof(owner->packet));
    memset(owner->control, 0, sizeof(owner->control));
    xQueueReset(owner->rx); /* destroy 已撤销全部旧回调，此时才可清队列。 */
    portENTER_CRITICAL(&status_lock);
    owner->status.turn_id = 0;
    owner->connected = owner->disconnected = false;
    owner->rx_error = ESP_OK;
    portEXIT_CRITICAL(&status_lock);
    return true;
}

static void fail_turn(wss_owner_t *owner, esp_err_t error, bool codec_failed)
{
    owner->failure_turn = owner->active_turn;
    portENTER_CRITICAL(&status_lock);
    owner->status.last_error = error;
    if (codec_failed) {
        owner->status.faulted = true;
        owner->status.ready = false;
    }
    portEXIT_CRITICAL(&status_lock);
    (void)close_turn(owner, false);
}

static void deliver_failure(wss_owner_t *owner)
{
    if (owner->failure_turn == 0) return;
    pet_ai_agent_snapshot_t snapshot;
    if (pet_ai_agent_snapshot(&snapshot) != ESP_OK) return;
    /* begin 回调与 A4 发布快照之间，网络可能已经失败。较旧的快照只表示
     * 生产者尚未发布，必须保留通知；只有更新轮次或终态才能淘汰它。 */
    if (!snapshot.faulted && snapshot.view.turn_id < owner->failure_turn) return;
    if (snapshot.faulted || snapshot.view.turn_id != owner->failure_turn ||
        (snapshot.view.state != PET_AI_LISTENING && snapshot.view.state != PET_AI_THINKING &&
         snapshot.view.state != PET_AI_SPEAKING)) {
        owner->failure_turn = 0;
        return;
    }
    if (pet_ai_agent_post(PET_AI_TRANSPORT_FAILED, owner->failure_turn) != ESP_ERR_TIMEOUT)
        owner->failure_turn = 0; /* 队满保留重试；新轮次/故障实例不接旧失败。 */
}

static esp_err_t open_turn(wss_owner_t *owner, uint32_t turn)
{
    owner->active_turn = turn;
    uint32_t epoch = 0;
    const pet_xz_result_t result = pet_xz_begin(&owner->protocol, turn, &epoch);
    owner->binding = (connection_binding_t){.owner = owner, .epoch = epoch};
    portENTER_CRITICAL(&status_lock);
    owner->status.turn_id = turn;
    owner->status.epoch = epoch;
    owner->callbacks_enabled = true;
    portEXIT_CRITICAL(&status_lock);
    if (result != PET_XZ_OK) return ESP_FAIL;
    const esp_websocket_client_config_t config = {
        .uri = owner->uri, .headers = owner->headers,
        .transport = WEBSOCKET_TRANSPORT_OVER_SSL,
        .disable_auto_reconnect = true, .enable_close_reconnect = false,
        .buffer_size = RX_CHUNK_BYTES, .task_stack = 6144, .task_prio = 4,
        .network_timeout_ms = 1000, .skip_cert_common_name_check = false,
        .cert_pem = owner->ca[0] != '\0' ? owner->ca : NULL,
        .crt_bundle_attach = owner->ca[0] == '\0' ? esp_crt_bundle_attach : NULL,
    };
    owner->started_ms = now_ms();
    owner->socket = esp_websocket_client_init(&config);
    if (owner->socket == NULL) return ESP_ERR_NO_MEM;
    esp_err_t error = esp_websocket_register_events(owner->socket, WEBSOCKET_EVENT_ANY,
                                                    on_socket, &owner->binding);
    if (error == ESP_OK) error = esp_websocket_client_start(owner->socket);
    return error;
}

static esp_err_t assemble(wss_owner_t *owner, const rx_item_t *item)
{
    if (item->opcode > 2 || item->payload_length < 0 || item->offset < 0 ||
        item->length < 0 || item->offset > item->payload_length ||
        item->length > item->payload_length - item->offset) return ESP_ERR_INVALID_ARG;
    if (item->offset == 0) {
        if (owner->frame_bytes != owner->frame_expected) return ESP_ERR_INVALID_ARG;
        if (item->opcode == 0) {
            if (!owner->message_active) return ESP_ERR_INVALID_ARG;
        } else {
            if (owner->message_active) return ESP_ERR_INVALID_ARG;
            owner->message_active = true;
            owner->message_opcode = item->opcode;
            owner->assembly_ms = now_ms();
        }
        owner->frame_bytes = 0;
        owner->frame_expected = (size_t)item->payload_length;
        owner->frame_opcode = item->opcode;
        owner->frame_fin = item->fin;
        const size_t limit = owner->message_opcode == 1 ? PET_XZ_TEXT_BYTES : PET_XZ_PACKET_BYTES;
        if ((size_t)item->payload_length > limit - owner->message_bytes)
            return ESP_ERR_INVALID_SIZE;
    } else if (!owner->message_active || owner->frame_opcode != item->opcode ||
               owner->frame_fin != item->fin || owner->frame_expected != (size_t)item->payload_length ||
               owner->frame_bytes != (size_t)item->offset) return ESP_ERR_INVALID_ARG;
    if (item->length == 0 && item->payload_length != 0) return ESP_ERR_INVALID_ARG;
    memcpy(owner->message + owner->message_bytes, item->data, (size_t)item->length);
    owner->message_bytes += (size_t)item->length;
    owner->frame_bytes += (size_t)item->length;
    if (owner->frame_bytes == owner->frame_expected && item->fin) owner->message_ready = true;
    return ESP_OK;
}

static esp_err_t mcp_failed(wss_owner_t *owner,esp_err_t error)
{
    if(owner->tool_response)memset(owner->tool_response,0,8192);
    portENTER_CRITICAL(&status_lock);
    ++owner->status.mcp_failed;
    portEXIT_CRITICAL(&status_lock);
    return error;
}

static esp_err_t receive_message(wss_owner_t *owner)
{
    pet_xz_result_t result;
    if (owner->message_opcode == 1) {
        pet_xz_event_t event;
        result = pet_xz_receive_text(&owner->protocol, owner->binding.epoch,
            (const char *)owner->message, owner->message_bytes, &event);
        if (result == PET_XZ_OK && event.kind == PET_XZ_EVENT_HELLO) {
            size_t bytes = 0;
            result = pet_xz_start_input(&owner->protocol, owner->binding.epoch,
                                      owner->control, sizeof(owner->control), &bytes);
            if (result == PET_XZ_OK) {
                const esp_err_t error = send_data(owner, owner->control, bytes, false);
                if (error != ESP_OK) return error;
            }
        } else if (result == PET_XZ_OK && event.kind == PET_XZ_EVENT_RESPONSE_BEGIN) {
            owner->response_pending = true;
        } else if (result==PET_XZ_OK && event.kind==PET_XZ_EVENT_TOOL && tools_handler) {
            portENTER_CRITICAL(&status_lock);
            ++owner->status.mcp_received;
            portEXIT_CRITICAL(&status_lock);
            size_t bytes=0;
            if (!tools_handler(event.epoch,event.rpc,strlen(event.rpc),owner->tool_response,8192,&bytes,tools_context))
                return mcp_failed(owner,ESP_ERR_INVALID_ARG);
            if (!bytes) { clear_message(owner); return ESP_OK; }
            cJSON *reply=cJSON_CreateObject();
            cJSON_AddStringToObject(reply,"session_id",owner->protocol.session_id);
            cJSON_AddStringToObject(reply,"type","mcp");
            cJSON_AddItemToObject(reply,"payload",cJSON_ParseWithLength(owner->tool_response,bytes));
            char *wire=cJSON_PrintUnformatted(reply); cJSON_Delete(reply);
            if (!wire) return mcp_failed(owner,ESP_ERR_NO_MEM);
            pet_ai_agent_snapshot_t privacy;
            bool blocked=pet_ai_agent_snapshot(&privacy)==ESP_OK&&
                (privacy.policy.muted||privacy.policy.dnd||privacy.policy.interrupted||privacy.policy_pending);
            esp_err_t sent=blocked?ESP_ERR_INVALID_STATE:send_data(owner,wire,strlen(wire),false);
            memset(wire,0,strlen(wire)); cJSON_free(wire);
            memset(owner->tool_response,0,8192);
            if (sent!=ESP_OK) return mcp_failed(owner,sent);
            portENTER_CRITICAL(&status_lock);
            ++owner->status.mcp_sent;
            portEXIT_CRITICAL(&status_lock);
        } else if (result==PET_XZ_OK && (event.kind==PET_XZ_EVENT_TRANSCRIPT || event.kind==PET_XZ_EVENT_REPLY)) {
            (void)pet_ai_agent_text(event.turn_id,event.kind==PET_XZ_EVENT_TRANSCRIPT ?
                PET_AI_TEXT_USER : PET_AI_TEXT_ASSISTANT,event.text);
        } else if (result == PET_XZ_OK && event.kind == PET_XZ_EVENT_REMOTE_FAILED) return ESP_FAIL;
    } else {
        result = pet_xz_receive_audio(&owner->protocol, owner->binding.epoch,
                                      owner->message, owner->message_bytes);
        if (result == PET_XZ_OK) {
            portENTER_CRITICAL(&status_lock);
            ++owner->status.received_packets;
            portEXIT_CRITICAL(&status_lock);
        }
    }
    if (result == PET_XZ_BUSY) return ESP_ERR_TIMEOUT;
    if (result == PET_XZ_CODEC_FAILED) return ESP_FAIL;
    if (result != PET_XZ_OK && result != PET_XZ_IGNORED && result != PET_XZ_STALE)
        return ESP_ERR_INVALID_ARG;
    clear_message(owner);
    return ESP_OK;
}

static esp_err_t flush_playback(wss_owner_t *owner)
{
    /* tts.start 只代表云端开始合成。首包 PCM 到达前保留 A4 的回复等待期限，
     * 否则会在真实音频尚未到达时触发它的 500 ms 播放停滞保护。 */
    if (owner->response_pending && owner->protocol.have_audio) {
        const esp_err_t error = pet_ai_agent_post(PET_AI_RESPONSE_BEGIN, owner->active_turn);
        if (error != ESP_OK) return error;
        owner->response_pending = false;
        owner->blocked_ms = 0;
    }
    /* 每包最多三个 20 ms 块和一个 finish；A4 队满时保留原 PCM。 */
    for (unsigned i = 0; i < 4; ++i) {
        pet_xz_playback_t playback;
        const pet_xz_result_t result = pet_xz_peek_playback(&owner->protocol,
                                                          owner->binding.epoch, &playback);
        if (result != PET_XZ_OK) return ESP_OK;
        const esp_err_t error = pet_ai_agent_play(owner->active_turn, playback.pcm,
                                                  playback.samples, playback.finish);
        if (error != ESP_OK) return error;
        if (pet_xz_commit_playback(&owner->protocol, owner->binding.epoch) != PET_XZ_OK)
            return ESP_FAIL;
        owner->blocked_ms = 0;
    }
    return ESP_OK;
}

static esp_err_t transmit(wss_owner_t *owner)
{
    for (unsigned i = 0; i < 8; ++i) {
        if (!owner->tx_pending)
            owner->tx_pending = xQueueReceive(owner->tx, &owner->outgoing, 0) == pdTRUE;
        if (!owner->tx_pending) break;
        portENTER_CRITICAL(&status_lock);
        const uint32_t cancelled = owner->cancelled_through;
        portEXIT_CRITICAL(&status_lock);
        /* 旧连接刚关闭、新连接尚未创建的间隙，已经入队的新轮次必须保留。 */
        if (owner->active_turn == 0 && owner->outgoing.turn_id > owner->completed_turn &&
            owner->outgoing.turn_id > cancelled) break;
        if (owner->outgoing.turn_id != owner->active_turn) {
            portENTER_CRITICAL(&status_lock);
            ++owner->status.stale_items;
            portEXIT_CRITICAL(&status_lock);
        } else {
            if (owner->protocol.phase != PET_XZ_INPUT) break;
            size_t bytes = 0, control_bytes = 0;
            const pet_xz_result_t result = owner->outgoing.end ?
                pet_xz_end_input(&owner->protocol, owner->binding.epoch,
                    owner->packet, sizeof(owner->packet), &bytes,
                    owner->control, sizeof(owner->control), &control_bytes) :
                pet_xz_capture(&owner->protocol, owner->binding.epoch,
                    owner->outgoing.pcm, PET_AUDIO_CAPTURE_SAMPLES,
                    owner->packet, sizeof(owner->packet), &bytes);
            if (result != PET_XZ_OK) return ESP_FAIL;
            esp_err_t error = send_data(owner, owner->packet, bytes, true);
            if (error == ESP_OK) error = send_data(owner, owner->control, control_bytes, false);
            if (error != ESP_OK) return error;
        }
        memset(&owner->outgoing, 0, sizeof(owner->outgoing));
        memset(owner->packet, 0, sizeof(owner->packet));
        owner->tx_pending = false;
    }
    return ESP_OK;
}

static esp_err_t receive(wss_owner_t *owner)
{
    bool empty = false;
    for (unsigned i = 0; i < 8; ++i) {
        esp_err_t error = flush_playback(owner);
        if (error != ESP_OK) return error;
        if (owner->message_ready) {
            error = receive_message(owner);
            if (error != ESP_OK) return error;
        }
        /* 允许解码首包；之后先将 RESPONSE_BEGIN 入 A4 再消费后续包。 */
        if (owner->response_pending && owner->protocol.have_audio) continue;
        rx_item_t item;
        if (xQueueReceive(owner->rx, &item, 0) != pdTRUE) { empty = true; break; }
        if (item.epoch != owner->binding.epoch) {
            portENTER_CRITICAL(&status_lock);
            ++owner->status.stale_items;
            portEXIT_CRITICAL(&status_lock);
            continue;
        }
        error = assemble(owner, &item);
        if (error != ESP_OK) return error;
    }
    portENTER_CRITICAL(&status_lock);
    const bool disconnected = owner->disconnected;
    portEXIT_CRITICAL(&status_lock);
    if (empty && disconnected && !owner->protocol.ended) return ESP_FAIL;
    return ESP_OK;
}

static void worker(void *context)
{
    wss_owner_t *owner = context;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    esp_err_t error = pet_opus_open(&owner->opus);
    if (error == ESP_OK) {
        const pet_xz_codec_t codec = pet_opus_port(&owner->opus);
        if (!pet_xz_init(&owner->protocol, &codec)) error = ESP_FAIL;
    }
    portENTER_CRITICAL(&status_lock);
    owner->status.last_error = error;
    owner->status.ready = error == ESP_OK;
    owner->status.faulted = error != ESP_OK;
    portEXIT_CRITICAL(&status_lock);
    if (error != ESP_OK) {
        const esp_err_t cleanup = pet_opus_close(&owner->opus);
        portENTER_CRITICAL(&status_lock);
        owner->status.cleanup_error = cleanup;
        portEXIT_CRITICAL(&status_lock);
        vTaskDelete(NULL);
        return;
    }
    for (;;) {
        const bool available = network_ready();
        portENTER_CRITICAL(&status_lock);
        owner->status.network_ready = available;
        const uint32_t requested = owner->requested_turn;
        const uint32_t cancelled = owner->cancelled_through;
        const bool connected = owner->connected;
        const bool disconnected = owner->disconnected;
        const esp_err_t rx_error = owner->rx_error;
        const bool faulted = owner->status.faulted;
        portEXIT_CRITICAL(&status_lock);
        deliver_failure(owner);
        if (faulted) {
            if (owner->failure_turn == 0) {
                const esp_err_t cleanup = pet_opus_close(&owner->opus);
                portENTER_CRITICAL(&status_lock);
                if (owner->status.cleanup_error == ESP_OK) owner->status.cleanup_error = cleanup;
                portEXIT_CRITICAL(&status_lock);
                vTaskDelete(NULL);
                return;
            }
        } else if (owner->active_turn != 0 &&
                   (owner->active_turn <= cancelled || owner->active_turn != requested)) {
            (void)close_turn(owner, true);
        } else if (owner->active_turn == 0 && requested > owner->completed_turn && requested > cancelled) {
            error = available ? open_turn(owner, requested) : ESP_ERR_INVALID_STATE;
            if (error != ESP_OK) {
                if (owner->active_turn == 0) owner->active_turn = requested;
                fail_turn(owner, error, !owner->opus.ready || owner->protocol.epoch == UINT32_MAX);
            }
        } else if (owner->active_turn != 0) {
            error = ESP_OK;
            if (rx_error != ESP_OK) error = rx_error;
            else if (disconnected && !owner->hello_sent) error = ESP_FAIL;
            else if (!available && !owner->protocol.ended) error = ESP_ERR_INVALID_STATE;
            else if (connected && !owner->hello_sent) {
                const char *hello = pet_xz_hello_tools(tools_handler!=NULL);
                error = send_data(owner, hello, strlen(hello), false);
                if (error == ESP_OK) owner->hello_sent = true;
            }
            if (error == ESP_OK && owner->hello_sent) error = receive(owner);
            if (error == ESP_ERR_TIMEOUT) {
                if (owner->blocked_ms == 0) owner->blocked_ms = now_ms();
                if (now_ms() - owner->blocked_ms < PLAYBACK_BLOCK_MS) error = ESP_OK;
            }
            if (error == ESP_OK && owner->assembly_ms != 0 &&
                now_ms() - owner->assembly_ms >= ASSEMBLY_MS) error = ESP_ERR_TIMEOUT;
            if (error == ESP_OK && owner->protocol.phase == PET_XZ_HELLO &&
                now_ms() - owner->started_ms >= HANDSHAKE_MS) error = ESP_ERR_TIMEOUT;
            if (error != ESP_OK) fail_turn(owner, error, !owner->opus.ready);
            else if (owner->protocol.phase == PET_XZ_DONE) (void)close_turn(owner, false);
        }
        if (!owner->status.faulted && transmit(owner) != ESP_OK)
            fail_turn(owner, ESP_FAIL, !owner->opus.ready);
#ifdef ESP_PLATFORM
        const uint32_t stack_free=uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&status_lock);
        owner->status.owner_stack_free=stack_free;
        portEXIT_CRITICAL(&status_lock);
#endif
        const TickType_t ticks = pdMS_TO_TICKS(5);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

esp_err_t pet_xz_wss_start(const pet_xz_wss_config_t *config, pet_ai_transport_t *transport)
{
    if (transport == NULL || !valid_config(config)) return ESP_ERR_INVALID_ARG;
    if (service != NULL) return ESP_ERR_INVALID_STATE; /* 组合根单一启动。 */
    wss_owner_t *owner = heap_caps_malloc(sizeof(*owner), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (owner == NULL) return ESP_ERR_NO_MEM;
    memset(owner, 0, sizeof(*owner));
    memcpy(owner->uri, config->uri, strlen(config->uri) + 1);
    const int bytes = snprintf(owner->headers, sizeof(owner->headers),
        "Authorization: %s%s\r\nProtocol-Version: 1\r\nDevice-Id: %s\r\nClient-Id: %s\r\n",
        strncmp(config->token, "Bearer ", 7) == 0 ? "" : "Bearer ", config->token,
        config->device_id, config->client_id);
    if (bytes < 0 || (size_t)bytes >= sizeof(owner->headers)) {
        heap_caps_free(owner);
        return ESP_ERR_INVALID_SIZE;
    }
    if (config->ca_cert_pem != NULL)
        memcpy(owner->ca, config->ca_cert_pem, strlen(config->ca_cert_pem) + 1);
    owner->tx = xQueueCreateWithCaps(TX_DEPTH, sizeof(tx_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    owner->rx = xQueueCreateWithCaps(RX_DEPTH, sizeof(rx_item_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (tools_handler) owner->tool_response=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    TaskHandle_t task = NULL;
    if ((tools_handler && !owner->tool_response) || owner->tx == NULL || owner->rx == NULL ||
        xTaskCreate(worker, "pet_xz_wss", WORKER_STACK_BYTES, owner, 4, &task) != pdPASS) {
        if (owner->tx != NULL) vQueueDeleteWithCaps(owner->tx);
        if (owner->rx != NULL) vQueueDeleteWithCaps(owner->rx);
        if (owner->tool_response) heap_caps_free(owner->tool_response);
        memset(owner, 0, sizeof(*owner));
        heap_caps_free(owner);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&status_lock);
    service = owner;
    portEXIT_CRITICAL(&status_lock);
    *transport = (pet_ai_transport_t){.context = owner, .begin_turn = begin_turn,
        .forward_pcm = forward_pcm, .end_input = end_input, .cancel_turn = cancel_turn};
    xTaskNotifyGive(task);
    return ESP_OK;
}

esp_err_t pet_xz_wss_snapshot(pet_xz_wss_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&status_lock);
    const wss_owner_t *owner = service;
    if (owner != NULL) *snapshot = owner->status;
    portEXIT_CRITICAL(&status_lock);
    return owner == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t pet_xz_wss_set_tools(pet_xz_tool_handler_t handler,void *context)
{
    if (service) return ESP_ERR_INVALID_STATE;
    tools_handler=handler; tools_context=context; return ESP_OK;
}
