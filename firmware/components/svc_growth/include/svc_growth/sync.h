#pragma once

#include <stddef.h>
#include "pet/growth.h"

#define PET_GROWTH_SYNC_MAX 1536u
/* 只读显示快照，不是恢复备份；unsaved=true 的数值可能在掉电后丢失。
 * boot_id 为每次启动的 32 位十六进制随机标识，sequence 在该启动内递增。
 * 返回 JSON 字节数；无对时、身份/数值无效或容量不足时返回 -1。 */
int pet_growth_sync_format(const char *device_id,const char *boot_id,uint32_t sequence,
    uint32_t observed_at,const pet_growth_view_t *view,char *out,size_t capacity);
