#pragma once

#include "pet/climate.h"

/* 首页温湿度卡片的纯逻辑快照：只做状态→文案映射，不依赖 LVGL。 */

typedef struct {
    pet_climate_status_t status;
    char temperature[24];
    char humidity[24];
    char comfort[24];
    char trend[32];
    char updated[32];
} pet_climate_card_t;

void pet_climate_card_render(const pet_climate_snapshot_t *snapshot, pet_climate_card_t *out);
