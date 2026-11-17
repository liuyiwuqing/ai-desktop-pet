#pragma once
#include "esp_err.h"
#include "esp_netif.h"

/* 配网模式和正常模式共用同一套家庭网络地址应用规则。
 * mode 为空或 "dhcp" 时保持自动获取；固定地址要求调用方已按 pet/netcfg.h 校验。 */
esp_err_t pet_netif_apply_ip(esp_netif_t *netif, const char *mode, const char *ip,
                             const char *netmask, const char *gateway, const char *dns,
                             const char *hostname);
