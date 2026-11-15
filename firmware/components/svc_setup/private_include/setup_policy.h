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
