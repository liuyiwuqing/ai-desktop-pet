#include "pet_ext/device_control.h"

static const char *const s_capabilities[] = {
    "device.discover",
    "system.info",
    "project.status",
    "app.open",
};

static const pet_ext_manifest_t s_manifest = {
    .id = "device_control",
    .version_major = 0,
    .version_minor = 1,
    .version_patch = 0,
    .contract_version = PET_EXT_CONTRACT_VERSION,
    .risk = PET_EXT_RISK_L1_REVERSIBLE,
    .capabilities = s_capabilities,
    .capability_count = (uint8_t)(sizeof(s_capabilities) / sizeof(s_capabilities[0])),
};

const pet_ext_manifest_t *pet_ext_device_control_manifest(void)
{
    return &s_manifest;
}

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"

#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "esp_log.h"

static const char *TAG = "ext_device_control";

void pet_ext_device_control_init(void)
{
    ESP_LOGI(TAG, "device_control extension started (contract v%d)", PET_EXT_CONTRACT_VERSION);
}
#endif
#endif
