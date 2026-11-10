#pragma once

#include "pet/extension.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 扩展清单（与 extension.json 对应），主机可测。 */
const pet_ext_manifest_t *pet_ext_device_control_manifest(void);

/* 仅在 CONFIG_PET_EXT_DEVICE_CONTROL=y 时定义；由组合根在启动阶段调用。 */
void pet_ext_device_control_init(void);

#ifdef __cplusplus
}
#endif
