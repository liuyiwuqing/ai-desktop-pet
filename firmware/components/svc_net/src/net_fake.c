#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "pet/event_bus.h"
#include "pet/hal/net.h"
#include "svc_net/api.h"

/* 假传输：无 Wi-Fi/MQTT 依赖，用于无硬件构建与 CI。 */

static const char *TAG = "net_fake";

static bool s_online;
static uint16_t s_reconnect_count;
static pet_net_rx_handler_t s_rx_handler;
static void *s_rx_context;

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

static esp_err_t fake_start(const pet_net_config_t *config)
{
    (void)config;
    publish_state(PET_NET_STATE_CONNECTING, PET_NET_ERROR_NONE);
    s_online = true;
    publish_state(PET_NET_STATE_ONLINE, PET_NET_ERROR_NONE);
    ESP_LOGI(TAG, "fake transport online");
    return ESP_OK;
}

static esp_err_t fake_stop(void)
{
    s_online = false;
    publish_state(PET_NET_STATE_IDLE, PET_NET_ERROR_NONE);
    return ESP_OK;
}

static esp_err_t fake_publish(const char *topic, const char *payload, size_t length, bool retain)
{
    if (!s_online) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "publish topic=%s bytes=%u retain=%d", topic, (unsigned)length,
             retain ? 1 : 0);
    (void)payload;
    return ESP_OK;
}

static bool fake_is_online(void) { return s_online; }

static esp_err_t fake_publish_confirmed(const char *topic, const char *payload, size_t length,
                                        bool retain, uint32_t timeout_ms)
{
    (void)timeout_ms;
    /* 仅 fake 配置模拟 Broker 确认，不能作为真实 MQTT 交付证据。 */
    return fake_publish(topic, payload, length, retain);
}

static esp_err_t fake_set_rx_handler(pet_net_rx_handler_t handler, void *context)
{
    s_rx_handler = handler;
    s_rx_context = context;
    return ESP_OK;
}

static bool fake_is_time_synced(void)
{
    return s_online;
}

/* 假传输没有真实链路，不上报地址与信号，避免把夹具值当成设备网络事实。 */
esp_err_t pet_net_local_ip(char *out, size_t capacity)
{
    (void)out;
    (void)capacity;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_rssi(int *dbm)
{
    (void)dbm;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_ssid(char *out, size_t capacity)
{
    (void)out;
    (void)capacity;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_channel(int *channel)
{
    (void)channel;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_bssid(char *out, size_t capacity)
{
    (void)out;
    (void)capacity;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_channel_width(int *mhz)
{
    (void)mhz;
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t pet_net_last_disconnect(int *reason, uint32_t *epoch_seconds)
{
    (void)reason;
    (void)epoch_seconds;
    return ESP_ERR_NOT_SUPPORTED;
}

const pet_net_t *pet_fake_net_driver(void)
{
    static const pet_net_t driver = {
        .id = "fake",
        .start = fake_start,
        .stop = fake_stop,
        .publish = fake_publish,
        .publish_confirmed = fake_publish_confirmed,
        .set_rx_handler = fake_set_rx_handler,
        .is_online = fake_is_online,
        .is_time_synced = fake_is_time_synced,
    };
    return &driver;
}
