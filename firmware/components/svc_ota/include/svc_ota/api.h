#pragma once
#include "svc_ota/manifest.h"
#include "esp_err.h"
const pet_ota_identity_t *pet_ota_identity(void);
const char *pet_ota_identity_marker(void);
typedef enum {PET_OTA_IDLE,PET_OTA_DOWNLOADING,PET_OTA_PENDING_REBOOT,PET_OTA_FAILED} pet_ota_state_t;
typedef struct {pet_ota_state_t state;esp_err_t error;uint32_t written_bytes,release_sequence;} pet_ota_snapshot_t;
/* 只接受物理维护命令；HTTPS 根地址与公钥在构建时固定。apply 返回排队结果，
 * pending_reboot 只切换下次启动分区，不自行重启。不写活动 app/model、不擦除 NVS；
 * 启动分区切换失败时可能保留已预留的升级序号，需使用更高序号重试。 */
esp_err_t pet_ota_start(void);
esp_err_t pet_ota_apply(const char *release_id);
esp_err_t pet_ota_snapshot(pet_ota_snapshot_t *);
esp_err_t pet_ota_reboot(void);
/* 每秒调用；新固件 10 秒基础健康通过后确认，否则 30 秒回滚。 */
void pet_ota_health_tick(bool healthy);
