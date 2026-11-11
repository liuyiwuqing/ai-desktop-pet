#include "app.h"

#include <inttypes.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "pet/event_bus.h"

#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "svc_growth/api.h"
#endif

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#include "pet/hal/climate_sensor.h"
#include "pet/presentation/climate_card.h"
#include "svc_climate/api.h"
#endif

#if CONFIG_PET_MODULE_NET_ENABLED
#include "pet/storage.h"
#include "svc_net/api.h"
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/device_control.h"
#endif

#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo.h"
#endif

static const char *TAG = "app";

#if CONFIG_PET_MODULE_NET_ENABLED
#if CONFIG_PET_NET_FAKE
#define NET_FALLBACK_SSID "fake-ssid"
#define NET_FALLBACK_WIFI_PASSWORD ""
#define NET_FALLBACK_BROKER_URI "mqtts://fake.local:8883"
#define NET_FALLBACK_MQTT_USERNAME ""
#define NET_FALLBACK_MQTT_PASSWORD ""
#else
#define NET_FALLBACK_SSID CONFIG_PET_NET_WIFI_SSID
#define NET_FALLBACK_WIFI_PASSWORD CONFIG_PET_NET_WIFI_PASSWORD
#define NET_FALLBACK_BROKER_URI CONFIG_PET_NET_BROKER_URI
#define NET_FALLBACK_MQTT_USERNAME CONFIG_PET_NET_MQTT_USERNAME
#define NET_FALLBACK_MQTT_PASSWORD CONFIG_PET_NET_MQTT_PASSWORD
#endif

/* NVS 凭据由配网写入；这里的静态缓冲区贯穿整个运行期。 */
static char s_wifi_ssid[PET_NET_SSID_MAX + 1];
static char s_wifi_password[PET_NET_PASSWORD_MAX + 1];
static char s_broker_uri[PET_NET_URI_MAX + 1];
static char s_device_id[PET_NET_DEVICE_ID_MAX + 1];
static char s_mqtt_username[PET_NET_USERNAME_MAX + 1];
static char s_mqtt_password[PET_NET_TOKEN_MAX + 1];
static char s_ca_cert_pem[PET_NET_CA_PEM_MAX + 1];

static void load_text(const char *namespace_name, const char *key, char *out,
                      size_t capacity, const char *fallback)
{
    esp_err_t err = pet_storage_load_str(namespace_name, key, out, capacity);
    if (err == ESP_OK && out[0] != '\0') {
        return;
    }
    if (fallback != NULL) {
        strlcpy(out, fallback, capacity);
    } else {
        out[0] = '\0';
    }
}

static bool load_net_config(pet_net_config_t *config)
{
    esp_err_t err = pet_storage_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(err));
        return false;
    }

    load_text("net", "wifi_ssid", s_wifi_ssid, sizeof(s_wifi_ssid),
              NET_FALLBACK_SSID);
    load_text("net", "wifi_pass", s_wifi_password, sizeof(s_wifi_password),
              NET_FALLBACK_WIFI_PASSWORD);
    load_text("nexus", "mqtt_uri", s_broker_uri, sizeof(s_broker_uri),
              NET_FALLBACK_BROKER_URI);
    load_text("nexus", "device_id", s_device_id, sizeof(s_device_id),
              CONFIG_PET_NET_DEVICE_ID);
    load_text("nexus", "mqtt_user", s_mqtt_username, sizeof(s_mqtt_username),
              NET_FALLBACK_MQTT_USERNAME);
    load_text("nexus", "mqtt_pass", s_mqtt_password, sizeof(s_mqtt_password),
              NET_FALLBACK_MQTT_PASSWORD);
    load_text("nexus", "mqtt_ca", s_ca_cert_pem, sizeof(s_ca_cert_pem), NULL);

    memset(config, 0, sizeof(*config));
    config->wifi_ssid = s_wifi_ssid;
    config->wifi_password = s_wifi_password;
    config->broker_uri = s_broker_uri;
    config->device_id = s_device_id;
    config->mqtt_username = s_mqtt_username;
    config->mqtt_password = s_mqtt_password;
    config->ca_cert_pem = s_ca_cert_pem[0] != '\0' ? s_ca_cert_pem : NULL;
    config->keepalive_s = CONFIG_PET_NET_KEEPALIVE_S;
    config->reconnect_timeout_ms = CONFIG_PET_NET_RECONNECT_MS;

    if (s_wifi_ssid[0] == '\0' || s_broker_uri[0] == '\0') {
        ESP_LOGW(TAG, "network not provisioned: set NVS net/wifi_ssid and "
                      "nexus/mqtt_uri (device=%s)", s_device_id);
        return false;
    }
    return true;
}

static void on_net_state(const pet_event_t *event)
{
    pet_net_event_t payload;
    if (event->payload_len < sizeof(payload)) {
        return;
    }
    memcpy(&payload, event->payload, sizeof(payload));

    if (payload.error == PET_NET_ERROR_NONE) {
        ESP_LOGI(TAG, "net | %s | reconnects=%u", pet_net_state_name(payload.state),
                 payload.reconnect_count);
    } else {
        ESP_LOGW(TAG, "net | %s | error=%s | reconnects=%u",
                 pet_net_state_name(payload.state),
                 pet_net_error_name(payload.error), payload.reconnect_count);
    }
}
#endif

static void on_boot(const pet_event_t *event)
{
    ESP_LOGI(TAG, "boot event: uptime=%" PRIu32 " ms, seq=%" PRIu32,
             event->timestamp_ms, event->sequence);
}

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
static const char *alert_text(pet_climate_alert_t alert)
{
    switch (alert) {
    case PET_CLIMATE_ALERT_COLD:
        return "cold";
    case PET_CLIMATE_ALERT_HOT:
        return "hot";
    case PET_CLIMATE_ALERT_DRY:
        return "dry";
    case PET_CLIMATE_ALERT_HUMID:
        return "humid";
    case PET_CLIMATE_ALERT_CONDENSATION:
        return "condensation";
    default:
        return "none";
    }
}

/* 首版以串口日志充当 UI 出口；LVGL 卡片后续复用同一快照。 */
static void on_climate_reading(const pet_event_t *event)
{
    pet_climate_reading_t reading;
    if (event->payload_len < sizeof(reading)) {
        return;
    }
    memcpy(&reading, event->payload, sizeof(reading));

    pet_climate_snapshot_t snapshot;
    if (pet_climate_get_snapshot(&snapshot) != ESP_OK) {
        return;
    }

    pet_climate_card_t card;
    pet_climate_card_render(&snapshot, &card);
    ESP_LOGI(TAG, "climate | %s | %s | %s | %s | %s | q=%u err=%u",
             card.temperature, card.humidity, card.comfort, card.trend, card.updated,
             reading.quality, reading.error_code);
}

static void on_climate_alert(const pet_event_t *event)
{
    pet_climate_alert_payload_t payload;
    if (event->payload_len < sizeof(payload)) {
        return;
    }
    memcpy(&payload, event->payload, sizeof(payload));
    ESP_LOGW(TAG, "climate alert: %s", alert_text(payload.alert));
}
#endif

esp_err_t app_start(void)
{
    ESP_LOGI(TAG, "reset_reason=%d, free_heap=%u, free_psram=%u",
             esp_reset_reason(),
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    ESP_ERROR_CHECK(pet_event_bus_init());
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_BOOT, on_boot));

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_READING, on_climate_reading));
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_ALERT, on_climate_alert));
#endif

#if CONFIG_PET_MODULE_GROWTH_ENABLED
    ESP_ERROR_CHECK(pet_growth_init());
#endif

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    ESP_ERROR_CHECK(pet_climate_init(pet_bsp_climate_sensor()));
    ESP_ERROR_CHECK(pet_climate_start());
#endif

#if CONFIG_PET_MODULE_NET_ENABLED
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_NET_STATE, on_net_state));
    pet_net_config_t net_config;
    if (load_net_config(&net_config)) {
        esp_err_t net_err = pet_net_init(&net_config);
        if (net_err == ESP_OK) {
            net_err = pet_net_start();
        }
        if (net_err != ESP_OK) {
            ESP_LOGW(TAG, "network start failed: %s", esp_err_to_name(net_err));
        }
    }
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
    pet_ext_device_control_init();
#endif

#if CONFIG_PET_EXT_SERVO
    pet_ext_servo_init();
#endif

    pet_event_t boot;
    pet_event_prepare(&boot, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_NORMAL);
    boot.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ESP_ERROR_CHECK(pet_event_bus_publish(&boot));

    ESP_LOGI(TAG, "skeleton started, idf=%s", esp_get_idf_version());
    return ESP_OK;
}
