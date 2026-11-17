#include "pet/hal/net.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "net_policy.h"
#include "pet/event_bus.h"
#include "svc_net/api.h"

/* Wi-Fi station + esp-mqtt over TLS：MQTT 3.1.1、QoS1、持久会话、LWT。 */

#define NET_MQTT_BUFFER_SIZE 4096
#define NET_MQTT_TASK_STACK 8192
#define NET_WIFI_BACKOFF_MAX_MS 30000
#define NET_SNTP_SYNC_TIMEOUT_MS 15000
#define NET_SNTP_SERVER "pool.ntp.org"

static const char *TAG = "net_esp";

static char s_offline_payload[160];

static const pet_net_config_t *s_config;
static esp_netif_t *s_netif;
static esp_mqtt_client_handle_t s_mqtt;
static esp_timer_handle_t s_retry_timer;
static esp_timer_handle_t s_sync_timer;
static bool s_running;
static bool s_stopping;
static bool s_mqtt_connected;
static bool s_mqtt_started;
static bool s_time_synced;
static bool s_sntp_started;
static bool s_handlers_registered;
static uint16_t s_reconnect_count;
static uint8_t s_disconnect_reason;
static uint32_t s_disconnect_epoch;
static uint32_t s_retry_delay_ms;
static char s_status_topic[PET_NET_TOPIC_MAX];
static char s_inbox_topic[PET_NET_TOPIC_MAX];
static char s_config_topic[PET_NET_TOPIC_MAX];
#if CONFIG_PET_OTA_REMOTE_ENABLED
static char s_ota_topic[PET_NET_TOPIC_MAX];
#endif

static pet_net_rx_handler_t s_rx_handler;
static void *s_rx_context;
static uint8_t s_rx_buffer[NET_MQTT_BUFFER_SIZE];
static char s_rx_topic[PET_NET_TOPIC_MAX];
static size_t s_rx_length;
static bool s_rx_dropping;

typedef struct {
    char topic[PET_NET_TOPIC_MAX];
    uint8_t payload[NET_MQTT_BUFFER_SIZE];
    size_t length;
    bool retained;
} net_rx_delivery_t;
static QueueHandle_t s_rx_queue;
static QueueHandle_t s_pubacks;
static SemaphoreHandle_t s_confirm_lock;
static void publish_status(bool online);
static esp_err_t esp_net_publish_confirmed(const char *topic, const char *payload, size_t length,
                                           bool retain, uint32_t timeout_ms);

/* MQTT 在持有自身 API 锁时调用 DATA 回调；消息中心的 NVS 事务转到独立任务，
 * 避免慢速 Flash 操作阻塞 MQTT 心跳与 PUBACK。 */
static void rx_task(void *arg)
{
    (void)arg;
    net_rx_delivery_t *item;
    int64_t last_status = 0;
    for (;;) {
        if (xQueueReceive(s_rx_queue, &item, pdMS_TO_TICKS(1000)) == pdTRUE) {
            if (s_running && s_rx_handler != NULL) {
                s_rx_handler(item->topic, (const char *)item->payload, item->length, item->retained,
                             s_rx_context);
            }
            heap_caps_free(item);
        }
        /* 在线快照需刷新 observedAt；也覆盖先连上 MQTT、稍后才完成 SNTP 的情况。
         * 复用生命周期锁，避免 stop 销毁 MQTT 时后台任务仍在入队。 */
        const int64_t now = esp_timer_get_time();
        if (now - last_status >= 30000000 && xSemaphoreTake(s_confirm_lock, 0) == pdTRUE) {
            if (s_running && !s_stopping && s_mqtt_connected && s_time_synced) {
                publish_status(true);
                last_status = now;
            }
            xSemaphoreGive(s_confirm_lock);
        }
    }
}

static void start_mqtt(void);

static void publish_state(pet_net_state_t state, pet_net_error_t error)
{
    pet_event_t event;
    pet_event_prepare(&event, PET_EVT_NET_STATE, PET_EVENT_SOURCE_NET,
                      PET_EVENT_PRIORITY_NORMAL);

    pet_net_event_t payload = {
        .state = state,
        .error = error,
        .reconnect_count = s_reconnect_count,
    };
    memcpy(event.payload, &payload, sizeof(payload));
    event.payload_len = sizeof(payload);
    event.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    (void)pet_event_bus_publish(&event);
}

static esp_err_t publish_qos1(const char *topic, const char *payload, size_t length,
                              bool retain)
{
    if (s_mqtt == NULL || !s_mqtt_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    /* 普通状态上报只入队；需等待 PUBACK 的持久回执走 publish_confirmed。 */
    int message_id =
        esp_mqtt_client_enqueue(s_mqtt, topic, payload, (int)length, 1, retain ? 1 : 0, true);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

static void publish_status(bool online)
{
    if (s_config == NULL)
        return;
    char payload[160];
    const int length = pet_net_presence_format(payload, sizeof(payload), s_config->device_id,
                                               online, s_time_synced ? (uint32_t)time(NULL) : 0);
    if (length > 0 && (size_t)length < sizeof(payload)) {
        (void)publish_qos1(s_status_topic, payload, (size_t)length, true);
    }
}

static void retry_timer_cb(void *arg)
{
    (void)arg;
    if (s_running) {
        (void)esp_wifi_connect();
    }
}

static void schedule_wifi_retry(void)
{
    if (!s_running || s_retry_timer == NULL || s_config == NULL) {
        return;
    }

    if (s_retry_delay_ms == 0) {
        s_retry_delay_ms = s_config->reconnect_timeout_ms;
    } else {
        uint32_t next = s_retry_delay_ms * 2U;
        s_retry_delay_ms = next > NET_WIFI_BACKOFF_MAX_MS ? NET_WIFI_BACKOFF_MAX_MS : next;
    }

    (void)esp_timer_stop(s_retry_timer);
    (void)esp_timer_start_once(s_retry_timer, (uint64_t)s_retry_delay_ms * 1000ULL);
}

static void start_mqtt(void)
{
    if (!s_running || s_mqtt == NULL || s_mqtt_started) {
        return;
    }
    esp_err_t err = esp_mqtt_client_start(s_mqtt);
    if (err == ESP_OK) {
        s_mqtt_started = true;
    } else {
        ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(err));
        publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_MQTT);
    }
}

static void sntp_sync_cb(struct timeval *tv)
{
    (void)tv;
    s_time_synced = true;
    if (s_sync_timer != NULL) {
        (void)esp_timer_stop(s_sync_timer);
    }
    ESP_LOGI(TAG, "sntp time synced");
    start_mqtt();
}

static void sntp_timeout_cb(void *arg)
{
    (void)arg;
    if (!s_running || s_time_synced) {
        return;
    }
    ESP_LOGW(TAG, "sntp not synced within %d ms; retry continues",
             NET_SNTP_SYNC_TIMEOUT_MS);
    start_mqtt();
}

static void start_time_sync(void)
{
    if (s_time_synced) {
        start_mqtt();
        return;
    }

    if (s_sync_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = sntp_timeout_cb,
            .name = "net_sntp_wait",
        };
        if (esp_timer_create(&timer_args, &s_sync_timer) != ESP_OK) {
            ESP_LOGW(TAG, "sntp timer create failed, mqtt starts in degraded mode");
            start_mqtt();
            return;
        }
    }
    (void)esp_timer_stop(s_sync_timer);
    (void)esp_timer_start_once(s_sync_timer,
                               (uint64_t)NET_SNTP_SYNC_TIMEOUT_MS * 1000ULL);

    if (s_sntp_started) {
        return;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NET_SNTP_SERVER);
    sntp_config.sync_cb = sntp_sync_cb;
    esp_err_t err = esp_netif_sntp_init(&sntp_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "sntp init failed: %s, mqtt starts in degraded mode",
                 esp_err_to_name(err));
        start_mqtt();
        return;
    }
    s_sntp_started = true;
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)base;

    if (event_id == WIFI_EVENT_STA_START) {
        (void)esp_wifi_connect();
        return;
    }

    if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = event_data;
        s_mqtt_connected = false;
        if (!s_running) {
            return;
        }
        /* 只记录运行期断线；正常 stop 的主动断开会先清 s_running，不算故障。 */
        s_disconnect_reason = event != NULL ? event->reason : 0;
        const time_t now = time(NULL);
        s_disconnect_epoch = now >= 1600000000 ? (uint32_t)now : 0;
        ESP_LOGW(TAG, "wifi disconnected, reason=%d", event != NULL ? event->reason : -1);
        publish_state(PET_NET_STATE_RECONNECTING, PET_NET_ERROR_WIFI);
        schedule_wifi_retry();
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)base;

    if (event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "wifi connected, ip=" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_delay_ms = 0;
        if (s_retry_timer != NULL) {
            (void)esp_timer_stop(s_retry_timer);
        }
        if (!s_running) {
            return;
        }
        publish_state(PET_NET_STATE_CONNECTING, PET_NET_ERROR_NONE);
        /* 消息 TTL/ACK 依赖系统时间：先对时，再连接 Broker。 */
        start_time_sync();
        return;
    }

    if (event_id == IP_EVENT_STA_LOST_IP && s_running) {
        s_mqtt_connected = false;
        publish_state(PET_NET_STATE_RECONNECTING, PET_NET_ERROR_WIFI);
    }
}

static pet_net_error_t mqtt_error_type(const esp_mqtt_event_handle_t event)
{
    if (event->error_handle == NULL) {
        return PET_NET_ERROR_MQTT;
    }
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        return event->error_handle->esp_tls_last_esp_err != ESP_OK ? PET_NET_ERROR_TLS
                                                                   : PET_NET_ERROR_WIFI;
    }
    return PET_NET_ERROR_MQTT;
}

static void mqtt_data_event(const esp_mqtt_event_handle_t event)
{
    if (event->data == NULL || event->data_len <= 0 || event->total_data_len <= 0) {
        return;
    }

    if (event->total_data_len > (int)sizeof(s_rx_buffer)) {
        ESP_LOGW(TAG, "mqtt payload too large: %d > %u", event->total_data_len,
                 (unsigned)sizeof(s_rx_buffer));
        s_rx_dropping = true;
        return;
    }

    if (event->current_data_offset == 0) {
        size_t topic_length = event->topic_len > 0 ? (size_t)event->topic_len : 0;
        if (topic_length >= sizeof(s_rx_topic)) {
            topic_length = sizeof(s_rx_topic) - 1;
        }
        if (event->topic != NULL && topic_length > 0) {
            memcpy(s_rx_topic, event->topic, topic_length);
        }
        s_rx_topic[topic_length] = '\0';
        s_rx_length = 0;
        s_rx_dropping = false;
    }

    if (event->current_data_offset < 0 || event->data_len < 0) {
        s_rx_dropping = true;
        return;
    }

    if (s_rx_dropping) {
        if (event->current_data_offset + event->data_len >= event->total_data_len) {
            s_rx_dropping = false;
        }
        return;
    }

    size_t offset = (size_t)event->current_data_offset;
    size_t chunk = (size_t)event->data_len;
    if (offset + chunk > sizeof(s_rx_buffer)) {
        s_rx_dropping = true;
        return;
    }
    memcpy(s_rx_buffer + offset, event->data, chunk);
    s_rx_length = offset + chunk;

    if (s_rx_length < (size_t)event->total_data_len) {
        return;
    }

    if (s_rx_handler != NULL && s_rx_topic[0] != '\0') {
        net_rx_delivery_t *item =
            heap_caps_malloc(sizeof(*item), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (item != NULL) {
            strlcpy(item->topic, s_rx_topic, sizeof(item->topic));
            memcpy(item->payload, s_rx_buffer, s_rx_length);
            item->length = s_rx_length;
            item->retained = event->retain;
        }
        if (item == NULL || xQueueSend(s_rx_queue, &item, 0) != pdTRUE) {
            heap_caps_free(item);
            ESP_LOGW(TAG, "inbox delivery busy; no application ACK, server may retry");
        }
    }
    s_rx_length = 0;
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id,
                               void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        ESP_LOGI(TAG, "mqtt connected, session_present=%d", event->session_present);
        publish_state(PET_NET_STATE_ONLINE, PET_NET_ERROR_NONE);
        publish_status(true);
        (void)esp_mqtt_client_subscribe(s_mqtt, s_inbox_topic, 1);
        (void)esp_mqtt_client_subscribe(s_mqtt, s_config_topic, 1);
#if CONFIG_PET_OTA_REMOTE_ENABLED
        (void)esp_mqtt_client_subscribe(s_mqtt, s_ota_topic, 1);
#endif
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        if (s_running) {
            s_reconnect_count++;
            ESP_LOGW(TAG, "mqtt disconnected, reconnects=%u",
                     (unsigned)s_reconnect_count);
            publish_state(PET_NET_STATE_RECONNECTING, PET_NET_ERROR_NONE);
        }
        break;

    case MQTT_EVENT_ERROR: {
        pet_net_error_t error = mqtt_error_type(event);
        ESP_LOGE(TAG, "mqtt error=%s tls_err=%s tls_stack=%d sock_errno=%d",
                 pet_net_error_name(error),
                 esp_err_to_name(event->error_handle != NULL
                                     ? event->error_handle->esp_tls_last_esp_err
                                     : ESP_OK),
                 event->error_handle != NULL ? event->error_handle->esp_tls_stack_err : 0,
                 event->error_handle != NULL ? event->error_handle->esp_transport_sock_errno
                                             : 0);
        if (s_running) {
            publish_state(PET_NET_STATE_RECONNECTING, error);
        }
        break;
    }

    case MQTT_EVENT_DATA:
        mqtt_data_event(event);
        break;

    case MQTT_EVENT_PUBLISHED:
        /* 回调只交接 packet ID，不等待业务锁，不在 MQTT 任务中写 NVS。
         * 队列满时不确认，由持久回执超时重试。 */
        (void)xQueueSend(s_pubacks, &event->msg_id, 0);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "mqtt subscribed, msg_id=%d", event->msg_id);
        break;

    default:
        break;
    }
}

static esp_err_t wifi_init_and_configure(const pet_net_config_t *config)
{
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    if (s_netif == NULL) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (s_netif == NULL) {
            return ESP_FAIL;
        }
    }

    if (!s_handlers_registered) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         wifi_event_handler, NULL);
        if (err != ESP_OK) {
            return err;
        }
        err = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                         ip_event_handler, NULL);
        if (err != ESP_OK) {
            return err;
        }
        s_handlers_registered = true;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {0};
    /* SDK 数组允许满 32/64 字节，不强行预留 NUL 而截断已校验的凭据。 */
    memcpy(wifi_config.sta.ssid, config->wifi_ssid, strlen(config->wifi_ssid));
    if (config->wifi_password)
        memcpy(wifi_config.sta.password, config->wifi_password, strlen(config->wifi_password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static esp_err_t mqtt_init(const pet_net_config_t *config)
{
    const int will_length = pet_net_presence_format(s_offline_payload, sizeof(s_offline_payload),
                                                     config->device_id, false, 0);
    if (will_length < 0)
        return ESP_ERR_INVALID_ARG;
    esp_mqtt_client_config_t mqtt_config = {0};
    mqtt_config.broker.address.uri = config->broker_uri;
    if (config->ca_cert_pem != NULL) {
        mqtt_config.broker.verification.certificate = config->ca_cert_pem;
    } else {
        mqtt_config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
    }

    mqtt_config.credentials.client_id = config->device_id;
    if (config->mqtt_username != NULL && config->mqtt_username[0] != '\0') {
        mqtt_config.credentials.username = config->mqtt_username;
    }
    if (config->mqtt_password != NULL && config->mqtt_password[0] != '\0') {
        mqtt_config.credentials.authentication.password = config->mqtt_password;
    }

    mqtt_config.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    mqtt_config.session.keepalive = config->keepalive_s;
    mqtt_config.session.disable_clean_session = true;
    mqtt_config.session.last_will.topic = s_status_topic;
    mqtt_config.session.last_will.msg = s_offline_payload;
    mqtt_config.session.last_will.msg_len = will_length;
    mqtt_config.session.last_will.qos = 1;
    mqtt_config.session.last_will.retain = 1;

    mqtt_config.network.reconnect_timeout_ms = (int)config->reconnect_timeout_ms;
    mqtt_config.network.timeout_ms = 10000;
    mqtt_config.buffer.size = NET_MQTT_BUFFER_SIZE;
    mqtt_config.buffer.out_size = NET_MQTT_BUFFER_SIZE;
    /* Broker 不确认时，重试也不能让内存 outbox 无限增长。 */
    mqtt_config.outbox.limit = NET_MQTT_BUFFER_SIZE;
    mqtt_config.task.stack_size = NET_MQTT_TASK_STACK;

    s_mqtt = esp_mqtt_client_init(&mqtt_config);
    if (s_mqtt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    return esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID,
                                          mqtt_event_handler, NULL);
}

static esp_err_t esp_net_start(const pet_net_config_t *config)
{
    if (s_running) {
        return ESP_OK;
    }

    esp_err_t err = ESP_OK;

    if (s_pubacks == NULL)
        s_pubacks = xQueueCreate(16, sizeof(int));
    if (s_confirm_lock == NULL)
        s_confirm_lock = xSemaphoreCreateMutex();
    if (s_pubacks == NULL || s_confirm_lock == NULL)
        return ESP_ERR_NO_MEM;

    if (s_rx_queue == NULL) {
        s_rx_queue = xQueueCreate(2, sizeof(net_rx_delivery_t *));
        if (s_rx_queue == NULL)
            return ESP_ERR_NO_MEM;
        if (xTaskCreate(rx_task, "pet_net_rx", 8192, NULL, 4, NULL) != pdPASS) {
            vQueueDelete(s_rx_queue);
            s_rx_queue = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    if (!pet_net_topic_build(PET_NET_TOPIC_STATUS, config->device_id, s_status_topic,
                             sizeof(s_status_topic)) ||
        !pet_net_topic_build(PET_NET_TOPIC_INBOX, config->device_id, s_inbox_topic,
                             sizeof(s_inbox_topic)) ||
        !pet_net_topic_build(PET_NET_TOPIC_CONFIG, config->device_id, s_config_topic,
                             sizeof(s_config_topic))) {
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_PET_OTA_REMOTE_ENABLED
    if(!pet_net_topic_build(PET_NET_TOPIC_OTA,config->device_id,s_ota_topic,sizeof(s_ota_topic)))return ESP_ERR_INVALID_ARG;
#endif

    if (s_retry_timer == NULL) {
        const esp_timer_create_args_t timer_args = {
            .callback = retry_timer_cb,
            .name = "net_wifi_retry",
        };
        err = esp_timer_create(&timer_args, &s_retry_timer);
        if (err != ESP_OK) {
            return err;
        }
    }

    err = wifi_init_and_configure(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi init failed: %s", esp_err_to_name(err));
        publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_WIFI);
        return err;
    }

    if (config->broker_uri && config->broker_uri[0]) {
        err = mqtt_init(config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "mqtt init failed: %s", esp_err_to_name(err));
            publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_MQTT);
            return err;
        }
    }

    s_config = config;
    s_reconnect_count = 0;
    s_disconnect_reason = 0;
    s_disconnect_epoch = 0;
    s_retry_delay_ms = 0;
    s_mqtt_connected = false;
    s_mqtt_started = false;
    s_time_synced = false;
    s_stopping = false;
    s_running = true;
    publish_state(PET_NET_STATE_CONNECTING, PET_NET_ERROR_NONE);

    err = esp_wifi_start();
    if (err != ESP_OK) {
        s_running = false;
        ESP_LOGE(TAG, "wifi start failed: %s", esp_err_to_name(err));
        publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_WIFI);
    }
    return err;
}

static esp_err_t esp_net_stop(void)
{
    if (!s_running) {
        return ESP_OK;
    }

    /* 正常 DISCONNECT 不触发 LWT；先有界确认 offline，停止心跳与新业务发布。 */
    s_stopping = true;
    if (s_mqtt_connected &&
        esp_net_publish_confirmed(s_status_topic, s_offline_payload, strlen(s_offline_payload),
                                   true, 3000) != ESP_OK)
        ESP_LOGW(TAG, "offline state not confirmed before shutdown");
    s_running = false;
    s_mqtt_connected = false;
    xSemaphoreTake(s_confirm_lock, portMAX_DELAY);
    if (s_retry_timer != NULL) {
        (void)esp_timer_stop(s_retry_timer);
    }
    if (s_sync_timer != NULL) {
        (void)esp_timer_stop(s_sync_timer);
    }
    if (s_sntp_started) {
        esp_netif_sntp_deinit();
        s_sntp_started = false;
    }
    s_time_synced = false;

    if (s_mqtt != NULL) {
        if (s_mqtt_started) {
            (void)esp_mqtt_client_stop(s_mqtt);
        }
        (void)esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
        s_mqtt_started = false;
    }

    (void)esp_wifi_stop();
    xSemaphoreGive(s_confirm_lock);
    publish_state(PET_NET_STATE_IDLE, PET_NET_ERROR_NONE);
    return ESP_OK;
}

static esp_err_t esp_net_publish(const char *topic, const char *payload, size_t length,
                                 bool retain)
{
    if (topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return publish_qos1(topic, payload, length, retain);
}

static esp_err_t esp_net_set_rx_handler(pet_net_rx_handler_t handler, void *context)
{
    s_rx_handler = handler;
    s_rx_context = context;
    return ESP_OK;
}

static esp_err_t esp_net_publish_confirmed(const char *topic, const char *payload, size_t length,
                                           bool retain, uint32_t timeout_ms)
{
    if (s_confirm_lock == NULL || s_pubacks == NULL)
        return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_confirm_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t result = ESP_ERR_INVALID_STATE;
    if (s_running && s_mqtt_connected && s_mqtt != NULL) {
        int acknowledged;
        while (xQueueReceive(s_pubacks, &acknowledged, 0) == pdTRUE) {
        }
        const int id =
            esp_mqtt_client_enqueue(s_mqtt, topic, payload, (int)length, 1, retain ? 1 : 0, true);
        result = id < 0 ? ESP_FAIL : ESP_ERR_TIMEOUT;
        const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
        while (id >= 0) {
            const int64_t remaining = deadline - esp_timer_get_time();
            if (remaining <= 0)
                break;
            if (xQueueReceive(s_pubacks, &acknowledged,
                              pdMS_TO_TICKS((uint32_t)((remaining + 999) / 1000))) != pdTRUE)
                break;
            if (acknowledged == id) {
                result = ESP_OK;
                break;
            }
        }
    }
    xSemaphoreGive(s_confirm_lock);
    return result;
}

static bool esp_net_is_online(void) { return s_running && !s_stopping && s_mqtt_connected; }

static bool esp_net_is_time_synced(void) { return s_time_synced; }

esp_err_t pet_net_local_ip(char *out, size_t capacity)
{
    if (out == NULL || capacity < 8) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_netif == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(s_netif, &info) != ESP_OK || info.ip.addr == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_ip4addr_ntoa(&info.ip, out, (int)capacity);
    return ESP_OK;
}

esp_err_t pet_net_rssi(int *dbm)
{
    if (dbm == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || ap.rssi > 0) {
        return ESP_ERR_INVALID_STATE;
    }
    *dbm = ap.rssi;
    return ESP_OK;
}

/* SSID 是空口字节，不保证是可打印 UTF-8；非法序列整条省略，
   否则一个坏字节就会让整份遥测 JSON 无法解析。 */
static bool ssid_printable_utf8(const uint8_t *text, size_t length)
{
    for (size_t i = 0; i < length;) {
        const uint8_t lead = text[i];
        if (lead < 0x20 || lead == 0x7f) {
            return false;
        }
        if (lead < 0x80) {
            i++;
            continue;
        }
        size_t extra;
        uint32_t code;
        if ((lead & 0xe0) == 0xc0) {
            extra = 1;
            code = lead & 0x1f;
        } else if ((lead & 0xf0) == 0xe0) {
            extra = 2;
            code = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0) {
            extra = 3;
            code = lead & 0x07;
        } else {
            return false;
        }
        if (i + extra >= length) {
            return false;
        }
        for (size_t k = 1; k <= extra; k++) {
            if ((text[i + k] & 0xc0) != 0x80) {
                return false;
            }
            code = (code << 6) | (text[i + k] & 0x3f);
        }
        if ((extra == 1 && code < 0x80) || (extra == 2 && code < 0x800) ||
            (extra == 3 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

esp_err_t pet_net_ssid(char *out, size_t capacity)
{
    if (out == NULL || capacity < 2) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }
    const size_t length = strnlen((const char *)ap.ssid, sizeof(ap.ssid));
    if (length == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (length + 1 > capacity) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!ssid_printable_utf8(ap.ssid, length)) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(out, ap.ssid, length);
    out[length] = '\0';
    return ESP_OK;
}

esp_err_t pet_net_channel(int *channel)
{
    if (channel == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK || ap.primary == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    *channel = ap.primary;
    return ESP_OK;
}

esp_err_t pet_net_last_disconnect(int *reason, uint32_t *epoch_seconds)
{
    if (reason == NULL || epoch_seconds == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_disconnect_reason == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    *reason = s_disconnect_reason;
    *epoch_seconds = s_disconnect_epoch;
    return ESP_OK;
}

const pet_net_t *pet_net_esp_driver(void)
{
    static const pet_net_t driver = {
        .id = "esp-mqtt",
        .start = esp_net_start,
        .stop = esp_net_stop,
        .publish = esp_net_publish,
        .publish_confirmed = esp_net_publish_confirmed,
        .set_rx_handler = esp_net_set_rx_handler,
        .is_online = esp_net_is_online,
        .is_time_synced = esp_net_is_time_synced,
    };
    return &driver;
}
