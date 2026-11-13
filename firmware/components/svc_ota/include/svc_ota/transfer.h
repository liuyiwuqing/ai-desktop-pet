#pragma once
#include "svc_ota/manifest.h"
/* 有界下载事务；输入须先通过签名/身份/版本校验。端口只写 inactive app。
 * finish 校验 HTTP 完整性及 SDK 镜像，verify 回读哈希/身份；reserve 持久化
 * sequence 并回读成功后才 commit。每条退出路径 cleanup 一次。 */
typedef struct {
    void *context;
    uint64_t (*now_ms)(void *);
    bool (*begin)(void *,const pet_ota_manifest_t *);
    bool (*read)(void *,uint8_t *,size_t,size_t *);
    bool (*write)(void *,const uint8_t *,size_t);
    bool (*finish)(void *);
    bool (*verify)(void *,const pet_ota_manifest_t *);
    bool (*reserve)(void *,uint32_t);
    bool (*commit)(void *);
    void (*cleanup)(void *);
    void (*progress)(void *,uint32_t);
} pet_ota_transfer_port_t;
bool pet_ota_transfer(const pet_ota_manifest_t *,const pet_ota_transfer_port_t *,
    uint64_t deadline_ms,uint8_t *buffer,size_t capacity);
