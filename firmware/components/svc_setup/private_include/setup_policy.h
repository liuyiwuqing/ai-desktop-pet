#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "svc_setup/api.h"

/* HTTP 边界策略可在主机验证，不访问 Wi-Fi 或 NVS。 */
bool pet_setup_request_authorized(const char *host, const char *origin,
                                  const char *token, const char *expected, bool mutation);
bool pet_setup_wifi_valid(const char *ssid, const char *password);
const char *pet_setup_state_name(pet_setup_state_t state);

/* 设备热点偏好：默认值、边界、生效热点与 NVS JSON 编解码都在主机覆盖。 */
void pet_setup_portal_default(pet_setup_portal_t *portal);
bool pet_setup_portal_valid(const pet_setup_portal_t *portal);
/* 只按当前策略读取字段；未启用的名称/口令不参与校验，也不写入编解码结果。 */
void pet_setup_portal_effective(const pet_setup_portal_t *portal, const uint8_t random[32],
                                char ssid[PET_SETUP_SSID_MAX + 1],
                                char password[PET_SETUP_PASSWORD_MAX + 1]);
bool pet_setup_portal_format(const pet_setup_portal_t *portal, char *out, size_t capacity);
bool pet_setup_portal_parse(const char *json, size_t bytes, pet_setup_portal_t *out);
/* 允许名称/口令留空，由调用方用已保存值补齐后再校验；用于“不回显密码”的页面。 */
bool pet_setup_portal_parse_partial(const char *json, size_t bytes, pet_setup_portal_t *out);

/* 家庭网络地址：与凭据校验、netif 应用共用 pet/netcfg.h 的规则。 */
void pet_setup_netcfg_default(pet_setup_netcfg_t *netcfg);
bool pet_setup_netcfg_valid(const pet_setup_netcfg_t *netcfg);
