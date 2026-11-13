#include "svc_net/api.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include "net_policy.h"
#include "pet/hal/net.h"

static const char *TAG = "svc_net";

static const pet_net_t *s_driver;
static const pet_net_config_t *s_config;

esp_err_t pet_net_init(const pet_net_config_t *config)
{
    if (s_driver != NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pet_net_error_t error = pet_net_config_validate(config);
    if (error != PET_NET_ERROR_NONE) {
        ESP_LOGE(TAG, "invalid network config: %s", pet_net_error_name(error));
        return ESP_ERR_INVALID_ARG;
    }

#if CONFIG_PET_NET_FAKE
    s_driver = pet_fake_net_driver();
#else
    s_driver = pet_net_esp_driver();
#endif
    if (s_driver == NULL) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    s_config = config;
    ESP_LOGI(TAG, "transport=%s device=%s broker=%s", s_driver->id,
             config->device_id, config->broker_uri);
    return ESP_OK;
}

esp_err_t pet_net_start(void)
{
    if (s_driver == NULL || s_config == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_driver->start(s_config);
}

esp_err_t pet_net_stop(void)
{
    if (s_driver == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_driver->stop();
}

bool pet_net_is_online(void)
{
    return s_driver != NULL && s_driver->is_online();
}

esp_err_t pet_net_set_rx_handler(pet_net_rx_handler_t handler, void *context)
{
    if (s_driver == NULL || s_driver->set_rx_handler == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return s_driver->set_rx_handler(handler, context);
}

esp_err_t pet_net_publish_topic(pet_net_topic_t topic, const char *payload,
                                size_t length, bool retain)
{
    if (s_driver == NULL || s_config == NULL || payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pet_net_is_online()) {
        return ESP_ERR_INVALID_STATE;
    }

    char topic_name[PET_NET_TOPIC_MAX];
    if (!pet_net_topic_build(topic, s_config->device_id, topic_name,
                             sizeof(topic_name))) {
        return ESP_ERR_INVALID_ARG;
    }
    return s_driver->publish(topic_name, payload, length, retain);
}

bool pet_net_is_time_synced(void)
{
    return s_driver != NULL && s_driver->is_time_synced != NULL &&
           s_driver->is_time_synced();
}

esp_err_t pet_net_publish_confirmed(pet_net_topic_t topic, const char *payload, size_t length,
                                    bool retain, uint32_t timeout_ms)
{
    if (s_driver == NULL || s_config == NULL || s_driver->publish_confirmed == NULL ||
        !pet_net_is_online())
        return ESP_ERR_INVALID_STATE;
    if (payload == NULL || length == 0 || timeout_ms == 0 || timeout_ms > 30000)
        return ESP_ERR_INVALID_ARG;
    char topic_name[PET_NET_TOPIC_MAX];
    if (!pet_net_topic_build(topic, s_config->device_id, topic_name, sizeof(topic_name)))
        return ESP_ERR_INVALID_ARG;
    return s_driver->publish_confirmed(topic_name, payload, length, retain, timeout_ms);
}
