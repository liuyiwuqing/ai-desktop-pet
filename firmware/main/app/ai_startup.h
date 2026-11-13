#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* 组合根传入已验证配置；启用 SR 时可开始本地监听，云端由唤醒/PTT 触发。 */
esp_err_t app_ai_start(uint8_t volume_pct, bool muted, bool dnd,
    esp_err_t (*load_str)(const char *, const char *, char *, size_t),
    void (*completed)(uint32_t));
