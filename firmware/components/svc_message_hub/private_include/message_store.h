#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "message_store_codec.h"

/* 双代 A/B 快照：每代包含去重环与完整消息视图。
 * 写入非活动代后再切换 active；读取失败回退另一代。 */
/* 初始化 NVS 与编码缓冲区；重复调用幂等。 */
esp_err_t pet_message_store_init(void);

/* 读取活动代；活动代损坏时回退另一代。无快照返回 ESP_ERR_NOT_FOUND。 */
esp_err_t pet_message_store_load(pet_message_snapshot_t *out);

/* 编码并写入非活动代，成功后再切换 active。 */
esp_err_t pet_message_store_save(const pet_message_snapshot_t *snapshot);
