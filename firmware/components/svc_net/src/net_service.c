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
