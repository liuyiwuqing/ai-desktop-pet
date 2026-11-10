#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 扩展包运行期清单：与 extensions/<name>/extension.json 对应。
 * 纯 C，不依赖 ESP-IDF，可在主机测试中直接编译。 */

#define PET_EXT_CONTRACT_VERSION 1
#define PET_EXT_CAPABILITY_MAX 16

typedef enum {
    PET_EXT_RISK_L0_READ = 0,
    PET_EXT_RISK_L1_REVERSIBLE,
    PET_EXT_RISK_L2_DESTRUCTIVE,
    PET_EXT_RISK_L3_CRITICAL,
    PET_EXT_RISK_COUNT,
} pet_ext_risk_t;

typedef struct {
    const char *id;
    uint16_t version_major;
    uint16_t version_minor;
    uint16_t version_patch;
    uint16_t contract_version;
    pet_ext_risk_t risk;
    const char *const *capabilities;
    uint8_t capability_count;
} pet_ext_manifest_t;

static inline bool pet_ext_manifest_is_valid(const pet_ext_manifest_t *manifest)
{
    if (manifest == NULL || manifest->id == NULL || manifest->capabilities == NULL) {
        return false;
    }
    return manifest->contract_version == PET_EXT_CONTRACT_VERSION &&
           manifest->risk >= PET_EXT_RISK_L0_READ && manifest->risk < PET_EXT_RISK_COUNT &&
           manifest->capability_count > 0 &&
           manifest->capability_count <= PET_EXT_CAPABILITY_MAX;
}

static inline bool pet_ext_manifest_token(const pet_ext_manifest_t *manifest, uint8_t index,
                                          const char **out)
{
    if (!pet_ext_manifest_is_valid(manifest) || index >= manifest->capability_count) {
        return false;
    }
    if (manifest->capabilities[index] == NULL || manifest->capabilities[index][0] == '\0') {
        return false;
    }
    *out = manifest->capabilities[index];
    return true;
}
