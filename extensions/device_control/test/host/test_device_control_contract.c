#include <stdio.h>
#include <string.h>

#include "pet/extension.h"
#include "pet_ext/device_control.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

int main(void)
{
    const pet_ext_manifest_t *manifest = pet_ext_device_control_manifest();

    CHECK(manifest != NULL);
    CHECK(manifest->id != NULL && strcmp(manifest->id, "device_control") == 0);
    CHECK(manifest->version_major == 0 && manifest->version_minor == 1);
    CHECK(manifest->contract_version == PET_EXT_CONTRACT_VERSION);
    CHECK(manifest->risk == PET_EXT_RISK_L2_DESTRUCTIVE);
    CHECK(pet_ext_manifest_is_valid(manifest));

    const char *capability = NULL;
    CHECK(pet_ext_manifest_token(manifest, 0, &capability));
    CHECK(capability != NULL && strcmp(capability, "device.discover") == 0);
    CHECK(!pet_ext_manifest_token(manifest, manifest->capability_count, &capability));
    CHECK(!pet_ext_manifest_is_valid(NULL));

    if (s_failures == 0) {
        printf("host tests passed: device_control extension contract\n");
    }
    return s_failures == 0 ? 0 : 1;
}
