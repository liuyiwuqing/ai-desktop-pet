#include "pet/hal/net.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

#include "net_policy.h"
#include "pet/event_bus.h"

/* Wi-Fi station + esp-mqtt over TLS：MQTT 3.1.1、QoS1、持久会话、LWT。 */

#define NET_MQTT_BUFFER_SIZE 4096
#define NET_MQTT_TASK_STACK 8192
#define NET_WIFI_BACKOFF_MAX_MS 30000

static const char *TAG = "net_esp";

static const char *const s_offline_payload = "{\"online\":false}";

static const pet_net_config_t *s_config;
static esp_netif_t *s_netif;
static esp_mqtt_client_handle_t s_mqtt;
static esp_timer_handle_t s_retry_timer;
static bool s_running;
static bool s_mqtt_connected;
static bool s_mqtt_started;
static bool s_handlers_registered;
static uint16_t s_reconnect_count;
static uint32_t s_retry_delay_ms;
static char s_status_topic[PET_NET_TOPIC_MAX];
static char s_inbox_topic[PET_NET_TOPIC_MAX];
static char s_config_topic[PET_NET_TOPIC_MAX];

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
    int message_id = esp_mqtt_client_publish(s_mqtt, topic, payload, (int)length, 1,
                                             retain ? 1 : 0);
    return message_id < 0 ? ESP_FAIL : ESP_OK;
}

static void publish_status(bool online)
{
    const esp_app_desc_t *description = esp_app_get_description();
    char payload[160];
    int length = snprintf(payload, sizeof(payload),
                          "{\"online\":%s,\"firmware\":\"%s\",\"reconnects\":%u}",
                          online ? "true" : "false", description->version,
                          (unsigned)s_reconnect_count);
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
        if (s_mqtt != NULL && !s_mqtt_started) {
            esp_err_t err = esp_mqtt_client_start(s_mqtt);
            if (err == ESP_OK) {
                s_mqtt_started = true;
            } else {
                ESP_LOGE(TAG, "mqtt start failed: %s", esp_err_to_name(err));
                publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_MQTT);
            }
        }
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
        ESP_LOGD(TAG, "mqtt data topic=%.*s bytes=%d/%d offset=%d",
                 event->topic_len, event->topic, event->data_len,
                 event->total_data_len, event->current_data_offset);
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
    strlcpy((char *)wifi_config.sta.ssid, config->wifi_ssid,
            sizeof(wifi_config.sta.ssid));
    strlcpy((char *)wifi_config.sta.password,
            config->wifi_password != NULL ? config->wifi_password : "",
            sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

static esp_err_t mqtt_init(const pet_net_config_t *config)
{
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
    mqtt_config.session.last_will.msg_len = (int)strlen(s_offline_payload);
    mqtt_config.session.last_will.qos = 1;
    mqtt_config.session.last_will.retain = 1;

    mqtt_config.network.reconnect_timeout_ms = (int)config->reconnect_timeout_ms;
    mqtt_config.network.timeout_ms = 10000;
    mqtt_config.buffer.size = NET_MQTT_BUFFER_SIZE;
    mqtt_config.buffer.out_size = NET_MQTT_BUFFER_SIZE;
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

    if (!pet_net_topic_build(PET_NET_TOPIC_STATUS, config->device_id,
                             s_status_topic, sizeof(s_status_topic)) ||
        !pet_net_topic_build(PET_NET_TOPIC_INBOX, config->device_id,
                             s_inbox_topic, sizeof(s_inbox_topic)) ||
        !pet_net_topic_build(PET_NET_TOPIC_CONFIG, config->device_id,
                             s_config_topic, sizeof(s_config_topic))) {
        return ESP_ERR_INVALID_ARG;
    }

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

    err = mqtt_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mqtt init failed: %s", esp_err_to_name(err));
        publish_state(PET_NET_STATE_OFFLINE, PET_NET_ERROR_MQTT);
        return err;
    }

    s_config = config;
    s_reconnect_count = 0;
    s_retry_delay_ms = 0;
    s_mqtt_connected = false;
    s_mqtt_started = false;
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

    s_running = false;
    s_mqtt_connected = false;
    if (s_retry_timer != NULL) {
        (void)esp_timer_stop(s_retry_timer);
    }

    if (s_mqtt != NULL) {
        if (s_mqtt_started) {
            (void)esp_mqtt_client_stop(s_mqtt);
        }
        (void)esp_mqtt_client_destroy(s_mqtt);
        s_mqtt = NULL;
        s_mqtt_started = false;
    }

    (void)esp_wifi_stop();
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

static bool esp_net_is_online(void)
{
    return s_running && s_mqtt_connected;
}

const pet_net_t *pet_net_esp_driver(void)
{
    static const pet_net_t driver = {
        .id = "esp-mqtt",
        .start = esp_net_start,
        .stop = esp_net_stop,
        .publish = esp_net_publish,
        .is_online = esp_net_is_online,
    };
    return &driver;
}
