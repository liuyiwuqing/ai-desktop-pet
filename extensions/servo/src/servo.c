#include "pet_ext/servo.h"

static const char *const s_capabilities[] = {
    "servo.move",
    "servo.stop",
    "servo.emergency_stop",
};

static const pet_ext_manifest_t s_manifest = {
    .id = "servo",
    .version_major = 0,
    .version_minor = 1,
    .version_patch = 0,
    .contract_version = PET_EXT_CONTRACT_VERSION,
    .risk = PET_EXT_RISK_L1_REVERSIBLE,
    .capabilities = s_capabilities,
    .capability_count = (uint8_t)(sizeof(s_capabilities) / sizeof(s_capabilities[0])),
};

const pet_ext_manifest_t *pet_ext_servo_manifest(void)
{
    return &s_manifest;
}

#if defined(ESP_PLATFORM)
#include "sdkconfig.h"

#if CONFIG_PET_EXT_SERVO
#include "esp_log.h"

static const char *TAG = "ext_servo";

void pet_ext_servo_init(void)
{
    ESP_LOGI(TAG,"servo extension registered; composition root owns startup");
}
#endif
#endif
