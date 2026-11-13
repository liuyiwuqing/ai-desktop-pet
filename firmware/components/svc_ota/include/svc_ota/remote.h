#pragma once
#include "svc_ota/api.h"

typedef struct pet_ota_remote pet_ota_remote_t;
typedef struct {
    void *context;
    esp_err_t (*load)(void *,void *,size_t,size_t *);
    esp_err_t (*save)(void *,const void *,size_t);
    esp_err_t (*apply)(void *,const pet_ota_target_t *);
    esp_err_t (*reboot)(void *);
    esp_err_t (*snapshot)(void *,pet_ota_snapshot_t *);
    pet_ota_image_state_t (*running)(void *,const pet_ota_target_t *);
    bool (*allowed)(void *);
} pet_ota_remote_port_t;

/* 单一拥有者 API，跨任务由调用方串行化；identity/port context 覆盖运行期。
 * init 失败时可能仍返回只读 handle，允许公开 unavailable 状态，不自动清空旧账本。
 * load 的不存在结果使用 ESP_ERR_NVS_NOT_FOUND，save 后始终做完整回读核对。
 * 不在 init/tick 自动恢复安装或重启动作；重启只可由独立、有效的新命令触发。 */
esp_err_t pet_ota_remote_init(pet_ota_remote_t **,const char *device,const char *boot_id,
    const pet_ota_identity_t *,const pet_ota_remote_port_t *);
void pet_ota_remote_close(pet_ota_remote_t *);
esp_err_t pet_ota_remote_command(pet_ota_remote_t *,const char *,size_t,bool retained,
    uint32_t epoch,char *reply,size_t capacity);
esp_err_t pet_ota_remote_tick(pet_ota_remote_t *);
esp_err_t pet_ota_remote_status(pet_ota_remote_t *,uint32_t epoch,char *,size_t);
