#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "pet/config_generated.h"

#define PET_CONFIG_JSON_MAX 768u
typedef struct {
    pet_config_t config;
    uint32_t revision;
    bool writable; /* 损坏或提交结果不明时保留数据，禁止继续覆盖。 */
    bool persisted;
} pet_config_snapshot_t;

void pet_config_defaults(pet_config_t *out);
esp_err_t pet_config_validate(const pet_config_t *config);
esp_err_t pet_config_parse_json(const char *json, size_t bytes, pet_config_t *out);
esp_err_t pet_config_format_json(const pet_config_t *config, char *out, size_t capacity, size_t *bytes);
/* 支持 Asia/Shanghai、UTC、Etc/UTC 与 UTC+HH:MM/UTC-HH:MM；固定偏移，不隐式处理 DST。 */
esp_err_t pet_config_utc_offset(const pet_config_t *config, int16_t *minutes);
/* enabled 表示启用定时 DND；相同起止为全天，未对时时按开启处理。 */
bool pet_config_dnd_active(const pet_config_t *config, uint32_t epoch_s, bool time_ready);

esp_err_t pet_config_init(void);
void pet_config_close(void); /* 停止所有调用方后才能关闭。 */
esp_err_t pet_config_snapshot(pet_config_snapshot_t *out); /* 非阻塞读取，忙返回 TIMEOUT。 */
esp_err_t pet_config_apply(const pet_config_t *config, uint32_t expected_revision);
esp_err_t pet_config_apply_json(const char *json, size_t bytes, uint32_t expected_revision);
esp_err_t pet_config_export_json(char *out, size_t capacity, size_t *bytes);

/* UI 不写 Flash：单一后台拥有者执行已复制的配置请求，完成结果由 token 查询。 */
esp_err_t pet_config_worker_start(void);
esp_err_t pet_config_submit(const pet_config_t *config, uint32_t expected_revision, uint32_t *token);
esp_err_t pet_config_result(uint32_t token, esp_err_t *result);

/* MQTT 工作任务使用，拒绝 retained/未对时/过期/跨设备/未知版本；只接受非凭据配置。
 * result 始终是可发布的 JSON，返回码只代表本地配置事务。 */
esp_err_t pet_config_remote(const char *device_id,const char *json,size_t bytes,bool retained,
    uint32_t epoch_s,char *result,size_t capacity);
