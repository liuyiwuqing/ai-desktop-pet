#include "svc_growth/api.h"

#include "sdkconfig.h"

#if CONFIG_PET_MODULE_GROWTH_ENABLED

#include "esp_log.h"

static const char *TAG = "svc_growth";

esp_err_t pet_growth_init(void)
{
    ESP_LOGI(TAG, "module ready (skeleton)");
    return ESP_OK;
}

#endif
