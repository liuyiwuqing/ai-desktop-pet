#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define PET_OTA_ENVELOPE_MAX 4096u
typedef enum {PET_OTA_VALID=0,PET_OTA_INVALID,PET_OTA_BAD_SIGNATURE,PET_OTA_INCOMPATIBLE,PET_OTA_ROLLBACK} pet_ota_validation_t;
typedef struct {
    const char *project,*board,*model_sha256,*partition_sha256;
    uint32_t config_schema,growth_schema;
} pet_ota_identity_t;
typedef struct {
    char version[33],image[65],image_sha256[65];
    uint32_t image_size,release_sequence;
} pet_ota_manifest_t;
typedef bool (*pet_ota_verify_t)(void *,const uint8_t *,size_t,const uint8_t *,size_t);
/* 签名覆盖原始 payload 字节；只接受 RSA-3072-PSS/SHA256 的固定 384 字节签名。
 * 不信任 manifest 中的下载 URL、公钥或设备身份，不允许写入单槽 model 分区。 */
pet_ota_validation_t pet_ota_manifest_parse(const char *,size_t,const pet_ota_identity_t *,
    uint32_t minimum_sequence,pet_ota_verify_t,void *,pet_ota_manifest_t *);
