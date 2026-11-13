#include "app.h"
#include "app_tools.h"
#include "app_diagnostics.h"
#include "app_behavior.h"
#include "app_recovery.h"
#include "app_ota_remote.h"

#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#include "sdkconfig.h"

#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "pet/event_bus.h"
#include "svc_config/api.h"
#include "svc_config/provision.h"
#include "svc_ota/api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "app_growth_sync.h"
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
#include "svc_ai_agent/api.h"
#endif
#include "nvs.h"

#if CONFIG_PET_AI_XIAOZHI_ON_BOOT
#include "ai_startup.h"
#endif

#if CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT
#include "svc_audio/api.h"
#endif

#if CONFIG_PET_MODULE_UI_ENABLED
#include "ui_lvgl/api.h"
#endif

#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "svc_growth/api.h"
#endif

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#include "pet/hal/climate_sensor.h"
#include "pet/presentation/climate_card.h"
#include "svc_climate/api.h"
#endif

#if CONFIG_PET_MODULE_NET_ENABLED
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pet/storage.h"
#include "svc_message_hub/api.h"
#include "svc_net/api.h"
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/device_control.h"
#include "pet_ext/control_service.h"
#endif

#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo.h"
#include "pet_ext/servo_service.h"
#include "pet/hal/pins.h"
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
static char s_device_id[PET_NET_DEVICE_ID_MAX + 1] = CONFIG_PET_NET_DEVICE_ID;
static char s_mqtt_username[PET_NET_USERNAME_MAX + 1];
static char s_mqtt_password[PET_NET_TOKEN_MAX + 1];
static char s_ca_cert_pem[PET_NET_CA_PEM_MAX + 1];

static void load_text(const char *namespace_name, const char *key, char *out,
                      size_t capacity, const char *fallback)
{
    esp_err_t err = pet_provision_load_str(namespace_name, key, out, capacity);
    if (err == ESP_OK && out[0] != '\0') {
        return;
    }
    if (fallback != NULL && err == ESP_ERR_NVS_NOT_FOUND) {
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

#if CONFIG_PET_MODULE_NET_ENABLED || CONFIG_PET_MODULE_UI_ENABLED || \
    CONFIG_PET_AI_XIAOZHI_ON_BOOT || CONFIG_PET_MESSAGE_TONES_ON_BOOT
static uint32_t app_epoch_now(void);
#endif

static void on_net_state(const pet_event_t *event)
{
    pet_net_event_t payload;
    if (event->payload_len < sizeof(payload)) {
        return;
    }
    memcpy(&payload, event->payload, sizeof(payload));

    if (payload.error == PET_NET_ERROR_NONE) {
        if (payload.state == PET_NET_STATE_ONLINE) {
            /* MQTT 可在 SNTP 超时后上线；未对时返回 0，清理函数会跳过。 */
            pet_message_hub_purge_expired(app_epoch_now());
        }
        ESP_LOGI(TAG, "net | %s | reconnects=%u", pet_net_state_name(payload.state),
                 payload.reconnect_count);
    } else {
        ESP_LOGW(TAG, "net | %s | error=%s | reconnects=%u",
                 pet_net_state_name(payload.state),
                 pet_net_error_name(payload.error), payload.reconnect_count);
    }
}

static const char *message_error_name(pet_message_error_t error)
{
    switch (error) {
    case PET_MESSAGE_OK:
        return "ok";
    case PET_MESSAGE_ERR_ARG:
        return "arg";
    case PET_MESSAGE_ERR_SCHEMA:
        return "schema";
    case PET_MESSAGE_ERR_ID:
        return "id";
    case PET_MESSAGE_ERR_SOURCE:
        return "source";
    case PET_MESSAGE_ERR_PRIORITY:
        return "priority";
    case PET_MESSAGE_ERR_TITLE:
        return "title";
    case PET_MESSAGE_ERR_BODY:
        return "body";
    case PET_MESSAGE_ERR_TIME:
        return "time";
    case PET_MESSAGE_ERR_DISPLAY:
        return "display";
    case PET_MESSAGE_ERR_EXPIRED:
        return "expired";
    case PET_MESSAGE_ERR_DUPLICATE:
        return "duplicate";
    case PET_MESSAGE_ERR_FULL:
        return "full";
    case PET_MESSAGE_ERR_STATE:
        return "state";
    case PET_MESSAGE_ERR_DELIVERY:
        return "persistence";
    default:
        return "unknown";
    }
}

/* 时间未同步时返回 0，暂不接受新投递或清理存档，等待对时与服务端重试。 */
static uint32_t app_epoch_now(void)
{
    return pet_net_is_time_synced() ? (uint32_t)time(NULL) : 0;
}

static esp_err_t publish_ack(const char *payload, size_t length, void *context)
{
    (void)context;
    return pet_net_publish_confirmed(PET_NET_TOPIC_ACK, payload, length, false, 3000);
}

static void message_ack_task(void *arg)
{
    (void)arg;
    uint32_t last_summary=0;
    for (;;) {
        pet_message_hub_purge_expired(app_epoch_now());
        if (pet_net_is_online()) {
            pet_message_hub_flush_acks();
            uint32_t epoch=app_epoch_now();
            app_ota_remote_poll(epoch);
            app_growth_sync_poll(epoch);
            if(epoch&&(!last_summary||epoch<last_summary||epoch-last_summary>=30)) {
                char summary[1536];
                if(app_summary_json(s_device_id,epoch,summary,sizeof(summary))==ESP_OK&&
                    pet_net_publish_topic(PET_NET_TOPIC_TELEMETRY,summary,strlen(summary),true)==ESP_OK)last_summary=epoch;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void on_net_message(const char *topic, const char *payload, size_t length,
                           bool retained, void *context)
{
    (void)context;
    if(app_ota_remote_message(topic,payload,length,retained,app_epoch_now()))return;
    char settings_topic[PET_NET_TOPIC_MAX];
    int written=snprintf(settings_topic,sizeof(settings_topic),PET_NEXUS_TOPIC_PREFIX "%s/config",s_device_id);
    if (written>0&&(size_t)written<sizeof(settings_topic)&&!strcmp(topic,settings_topic)) {
        char reply[256];
        (void)pet_config_remote(s_device_id,payload,length,retained,app_epoch_now(),reply,sizeof(reply));
        (void)pet_net_publish_confirmed(PET_NET_TOPIC_CONFIG_ACK,reply,strlen(reply),false,3000);
        return;
    }
    const pet_message_error_t err = pet_message_hub_receive(topic, payload, length, retained);
    if (err != PET_MESSAGE_OK) {
        ESP_LOGW(TAG, "inbox rejected: %s", message_error_name(err));
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

/* 串口保留传感器诊断摘要，界面使用同一服务快照。 */
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

#if !CONFIG_PET_MODULE_NET_ENABLED && (CONFIG_PET_MODULE_UI_ENABLED || \
    CONFIG_PET_AI_XIAOZHI_ON_BOOT || CONFIG_PET_MESSAGE_TONES_ON_BOOT)
static uint32_t app_epoch_now(void)
{
    time_t epoch=time(NULL);
    return epoch>=1577836800 && (uint64_t)epoch<=UINT32_MAX ? (uint32_t)epoch : 0;
}
#endif

static void settings_runtime(void *arg)
{
    (void)arg;
#if !CONFIG_PET_MODULE_UI_ENABLED && CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
    uint32_t previous=UINT32_MAX;
#endif
    for (;;) {
        pet_config_snapshot_t settings;
        if (pet_config_snapshot(&settings)==ESP_OK && settings.writable) {
#if CONFIG_PET_MODULE_NET_ENABLED
            (void)pet_message_hub_set_limit(settings.config.message.queue_depth);
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
            int16_t minutes;
            if (pet_config_utc_offset(&settings.config,&minutes)==ESP_OK)
                (void)pet_growth_set_utc_offset(minutes);
#endif
#if !CONFIG_PET_MODULE_UI_ENABLED && CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
            pet_ai_agent_snapshot_t ai;
            if (pet_ai_agent_snapshot(&ai)==ESP_OK) {
                (void)pet_ai_agent_set_volume(settings.config.volume);
                if (previous!=settings.revision)
                    (void)pet_ai_agent_post(settings.config.muted?PET_AI_MUTE:PET_AI_UNMUTE,0);
                uint32_t epoch=(uint32_t)time(NULL);
                (void)pet_ai_agent_set_dnd(pet_config_dnd_active(&settings.config,epoch,epoch>=1577836800));
                previous=settings.revision;
            }
#endif
        }
#if CONFIG_PET_OTA_ENABLED || CONFIG_PET_RECOVERY_ENABLED
        bool healthy=pet_config_snapshot(&settings)==ESP_OK&&settings.writable;
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        pet_growth_view_t growth;
        healthy=healthy&&pet_growth_get_snapshot(&growth)==PET_GROWTH_OK&&growth.storage_ready;
#endif
#if CONFIG_PET_MODULE_UI_ENABLED
        pet_ui_metrics_t ui;pet_ui_metrics(&ui);
        healthy=healthy&&ui.frames>0&&ui.failed_frames==0;
#endif
#if CONFIG_PET_AI_XIAOZHI_ON_BOOT
        pet_ai_agent_snapshot_t ai_health;
        healthy=healthy&&pet_ai_agent_snapshot(&ai_health)==ESP_OK&&ai_health.ready&&!ai_health.faulted&&ai_health.cleanup_error==ESP_OK;
#endif
        app_recovery_tick(healthy);
#if CONFIG_PET_OTA_ENABLED
        pet_ota_health_tick(healthy);
#endif
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#if CONFIG_PET_RECOVERY_ENABLED
#if CONFIG_PET_MODULE_UI_ENABLED
static void recovery_restart(void *context)
{
    (void)context;
    app_recovery_clear();
    esp_restart();
}
#endif
static void recovery_maintenance(void *arg)
{
    (void)arg;
    for(;;) {
#if CONFIG_PET_OTA_ENABLED
        // A failed candidate in maintenance mode must not confirm itself as healthy.
        pet_ota_health_tick(false);
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
#endif

#if CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT
static bool audio_diagnostic_allowed(void *context)
{
    (void)context;
    pet_config_snapshot_t settings;
    if (pet_config_snapshot(&settings) != ESP_OK || !settings.writable || settings.config.muted)
        return false;
    const uint32_t epoch = app_epoch_now();
    return !pet_config_dnd_active(&settings.config, epoch, epoch != 0);
}
#endif

esp_err_t app_start(void)
{
    const bool recovery=app_recovery_start();
    ESP_LOGI(TAG, "reset_reason=%d, free_heap=%u, free_psram=%u",
             esp_reset_reason(),
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG,"firmware identity: %s",pet_ota_identity_marker());

    ESP_ERROR_CHECK(pet_event_bus_init());
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_BOOT, on_boot));
    esp_err_t settings_error = pet_config_init();
    if (settings_error == ESP_OK) settings_error = pet_config_worker_start();
    if (settings_error != ESP_OK)
        ESP_LOGW(TAG, "settings unavailable: %s", esp_err_to_name(settings_error));
    (void)pet_config_console_set_handler(app_maintenance_command);
    const esp_err_t console_error = pet_config_console_start();
    if (console_error != ESP_OK) ESP_LOGW(TAG, "configuration console unavailable: %s", esp_err_to_name(console_error));

    if(recovery) {
        ESP_LOGE(TAG,"USB recovery active: optional services isolated; inspect PET_DIAGNOSTICS before PET_RECOVERY_CLEAR and manual restart");
#if CONFIG_PET_RECOVERY_ENABLED
        if(xTaskCreate(recovery_maintenance,"pet_recovery",4096,NULL,2,NULL)!=pdPASS)
            return ESP_ERR_NO_MEM;
#if CONFIG_PET_MODULE_UI_ENABLED
        // Consumed in RTC before display initialization; another crash must leave USB usable.
        if(app_recovery_claim_ui()) {
            esp_err_t error=pet_ui_recovery_start(pet_bsp_display(),pet_bsp_confirm_button(),recovery_restart,NULL);
            if(error!=ESP_OK)ESP_LOGW(TAG,"recovery display unavailable: %s",esp_err_to_name(error));
        } else ESP_LOGW(TAG,"recovery display already attempted; serial maintenance only");
#endif
#endif
        return ESP_OK;
    }

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_READING, on_climate_reading));
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_CLIMATE_ALERT, on_climate_alert));
#endif

#if CONFIG_PET_MODULE_GROWTH_ENABLED
    const esp_err_t growth_err = pet_growth_start();
    app_growth_sync_init(s_device_id);
    if (growth_err != ESP_OK)
        ESP_LOGW(TAG, "growth unavailable: %s", esp_err_to_name(growth_err));
#endif

    if (app_behavior_start()!=ESP_OK) ESP_LOGW(TAG,"behavior worker unavailable");

#if CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT
    const esp_err_t audio_err = pet_audio_diagnostic_start(audio_diagnostic_allowed, NULL);
    if (audio_err != ESP_OK)
        ESP_LOGW(TAG, "audio diagnostic unavailable: %s", esp_err_to_name(audio_err));
#endif

#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    esp_err_t climate_err = pet_climate_init(pet_bsp_climate_sensor());
    if (climate_err == ESP_OK) {
        climate_err = pet_climate_start();
    }
    if (climate_err != ESP_OK) {
        /* NVS 故障不擦除存档，也不让可选传感器阻断消息界面启动。 */
        ESP_LOGW(TAG, "climate unavailable: %s", esp_err_to_name(climate_err));
    }
#endif

#if CONFIG_PET_MODULE_NET_ENABLED
    ESP_ERROR_CHECK(pet_event_bus_subscribe(PET_EVT_NET_STATE, on_net_state));

    /* 配置与身份都在启动收件前固定；配置字符串必须覆盖整个运行期。 */
    static pet_net_config_t net_config;
    const bool net_configured = load_net_config(&net_config);
    esp_err_t msg_err = pet_message_hub_init(s_device_id, publish_ack, NULL, app_epoch_now);
    if (msg_err != ESP_OK) {
        ESP_LOGW(TAG, "message hub init failed: %s", esp_err_to_name(msg_err));
    }

    if (net_configured && msg_err == ESP_OK) {
        esp_err_t net_err = pet_net_init(&net_config);
        if (net_err == ESP_OK) {
            net_err = pet_net_set_rx_handler(on_net_message, NULL);
        }
        if (net_err == ESP_OK) {
            net_err = pet_net_start();
        }
        if (net_err != ESP_OK) {
            ESP_LOGW(TAG, "network start failed: %s", esp_err_to_name(net_err));
        } else if (msg_err == ESP_OK &&
                   xTaskCreate(message_ack_task, "pet_msg_ack", 6144, NULL, 3, NULL) != pdPASS) {
            ESP_LOGE(TAG, "ACK worker unavailable; durable receipts retained until restart");
        }
    }
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
    pet_ext_device_control_init();
    esp_err_t control_error=pet_control_service_start(s_device_id,pet_provision_load_str);
    if(control_error!=ESP_OK)ESP_LOGW(TAG,"device control unavailable: %s",esp_err_to_name(control_error));
#endif

#if CONFIG_PET_EXT_SERVO
    pet_ext_servo_init();
    if(CONFIG_PET_SERVO_GPIO>=0) {
        esp_err_t servo_error=pet_servo_service_start(pet_bsp_gpio_available);
        if(servo_error!=ESP_OK)ESP_LOGW(TAG,"servo unavailable: %s",esp_err_to_name(servo_error));
    }
#endif

#if CONFIG_PET_AI_XIAOZHI_ON_BOOT
    (void)app_tools_start();
    pet_config_snapshot_t settings;
    esp_err_t ai_err = pet_config_snapshot(&settings);
    if (ai_err == ESP_OK && !settings.writable) ai_err = ESP_ERR_INVALID_STATE;
    if (ai_err == ESP_OK) ai_err = app_ai_start(settings.config.volume, settings.config.muted,
        pet_config_dnd_active(&settings.config, app_epoch_now(), app_epoch_now() != 0), pet_provision_load_str,app_behavior_dialogue);
    if (ai_err != ESP_OK)
        ESP_LOGW(TAG, "AI services unavailable: %s", esp_err_to_name(ai_err));
#endif

#if CONFIG_PET_MESSAGE_TONES_ON_BOOT
    pet_config_snapshot_t tone_settings;
    if(pet_config_snapshot(&tone_settings)==ESP_OK&&tone_settings.writable) {
        const pet_ai_agent_config_t tones={.audio=pet_bsp_audio(),.notifications_only=true,
            .volume_pct=tone_settings.config.volume,.initial_policy={.muted=tone_settings.config.muted,
            .dnd=pet_config_dnd_active(&tone_settings.config,app_epoch_now(),app_epoch_now()!=0)}};
        esp_err_t tone_error=pet_ai_agent_start(&tones);
        if(tone_error!=ESP_OK)ESP_LOGW(TAG,"notification audio unavailable: %s",esp_err_to_name(tone_error));
    }
#endif

#if CONFIG_PET_MODULE_UI_ENABLED
    const esp_err_t ui_err =
        pet_ui_start(pet_bsp_display(), pet_bsp_confirm_button(), app_epoch_now);
    if (ui_err != ESP_OK) {
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
        /* 显示任务尚未创建时也必须停止无可见状态的录音，不能只依赖 UI 内部故障路径。 */
        (void)pet_ai_agent_set_interrupted(true);
#endif
        ESP_LOGW(TAG, "UI start failed: %s", esp_err_to_name(ui_err));
    }
#endif

#if CONFIG_PET_OTA_ENABLED
    esp_err_t ota_error=pet_ota_start();
    if(ota_error!=ESP_OK)ESP_LOGW(TAG,"OTA unavailable: %s",esp_err_to_name(ota_error));
#if CONFIG_PET_OTA_REMOTE_ENABLED
    if(ota_error==ESP_OK)app_ota_remote_init(s_device_id);
#endif
#endif

    if (xTaskCreate(settings_runtime,"pet_settings_rt",4096,NULL,2,NULL)!=pdPASS)
        ESP_LOGE(TAG,"runtime settings worker unavailable");

    pet_event_t boot;
    pet_event_prepare(&boot, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_NORMAL);
    boot.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    ESP_ERROR_CHECK(pet_event_bus_publish(&boot));

    ESP_LOGI(TAG, "skeleton started, idf=%s", esp_get_idf_version());
    return ESP_OK;
}
