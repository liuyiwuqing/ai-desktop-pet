#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define PET_PROVISION_JSON_MAX 10000u
/* 完整凭据包，只从物理配置入口接收；不提供凭据导出或日志。单个 NVS blob 原子替换。 */
esp_err_t pet_provision_save(const char *json, size_t bytes, uint32_t expected_revision);
esp_err_t pet_provision_revision(uint32_t *revision);
/* 本地配网页按组更新，保留未涉及的服务及固定设备身份；首次可只保存 net。
 * 不提供凭据回显，沿用同一 revision/CAS 与单 blob 原子写入。 */
esp_err_t pet_provision_patch(const char *json, size_t bytes, uint32_t expected_revision);
/* 无新包时兼容既有 namespace/key；包存在但损坏/缺键时不拼接旧凭据。 */
esp_err_t pet_provision_load_str(const char *namespace_name, const char *key, char *out, size_t capacity);
/* 只返回状态码/非敏感配置；输入缓冲由调用方在返回后清零。 */
esp_err_t pet_config_command(const char *line, size_t bytes, char *response, size_t capacity);
esp_err_t pet_config_console_start(void);

#define PET_MAINTENANCE_RESPONSE_MAX 8192u
typedef esp_err_t (*pet_maintenance_handler_t)(const char *,size_t,char *,size_t);
/* 组合根仅在 console_start 前注册，避免配置模块依赖具体业务模块。 */
esp_err_t pet_config_console_set_handler(pet_maintenance_handler_t handler);
