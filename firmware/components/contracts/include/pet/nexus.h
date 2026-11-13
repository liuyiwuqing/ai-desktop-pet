#pragma once

#include <stdbool.h>
#include <stddef.h>

/* nexus-core v1 的线协议边界；网络与消息组件共享，避免两份身份校验漂移。 */
#define PET_NEXUS_DEVICE_ID_MAX 40
#define PET_NEXUS_UUID_LENGTH 36
#define PET_NEXUS_PACKET_MAX 1024
#define PET_NEXUS_TOPIC_PREFIX "nexus/v1/devices/"

static inline bool pet_nexus_device_id_valid(const char *text)
{
    if (text == NULL || text[0] == '\0')
        return false;
    for (size_t i = 0; i <= PET_NEXUS_DEVICE_ID_MAX; ++i) {
        const char c = text[i];
        if (c == '\0')
            return true;
        if (i == PET_NEXUS_DEVICE_ID_MAX ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return false;
    }
    return false;
}

/* 服务端 UUID.toString() 使用小写标准形式，拒绝缩写及大小写别名。 */
static inline bool pet_nexus_uuid_valid(const char *text)
{
    if (text == NULL)
        return false;
    for (size_t i = 0; i < PET_NEXUS_UUID_LENGTH; ++i) {
        const char c = text[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return text[PET_NEXUS_UUID_LENGTH] == '\0';
}

static inline bool pet_nexus_token_valid(const char *text, size_t max_length)
{
    if (text == NULL || text[0] == '\0')
        return false;
    for (size_t i = 0; i <= max_length; ++i) {
        const char c = text[i];
        if (c == '\0')
            return true;
        if (i == max_length ||
            !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'))
            return false;
    }
    return false;
}
