#include "pet/presentation/climate_card.h"

#include <stdio.h>
#include <string.h>

static void format_tenths(char *out, size_t size, float value, const char *unit)
{
    const bool negative = value < 0.0f;
    const int tenths = (int)((negative ? -value : value) * 10.0f + 0.5f);
    snprintf(out, size, "%s%d.%d %s", negative ? "-" : "", tenths / 10, tenths % 10, unit);
}

static const char *comfort_text(float temperature_c, float humidity_pct)
{
    if (temperature_c > 30.0f) {
        return "偏热";
    }
    if (temperature_c < 10.0f) {
        return "偏冷";
    }
    if (humidity_pct > 80.0f) {
        return "潮湿";
    }
    if (humidity_pct < 30.0f) {
        return "干燥";
    }
    if (temperature_c >= 18.0f && temperature_c <= 26.0f) {
        return "舒适";
    }
    return "一般";
}

static void format_trend(char *out, size_t size, float delta, const char *unit)
{
    if (delta > -0.05f && delta < 0.05f) {
        snprintf(out, size, "—");
        return;
    }

    char value[24];
    format_tenths(value, sizeof(value), delta > 0.0f ? delta : -delta, unit);
    snprintf(out, size, "%s %s", delta > 0.0f ? "↑" : "↓", value);
}

void pet_climate_card_render(const pet_climate_snapshot_t *snapshot, pet_climate_card_t *out)
{
    if (out == NULL) {
        return;
    }

    memset(out, 0, sizeof(*out));

    if (snapshot == NULL) {
        out->status = PET_CLIMATE_STATUS_NO_DATA;
        snprintf(out->temperature, sizeof(out->temperature), "--");
        snprintf(out->humidity, sizeof(out->humidity), "--");
        snprintf(out->comfort, sizeof(out->comfort), "--");
        snprintf(out->trend, sizeof(out->trend), "—");
        snprintf(out->updated, sizeof(out->updated), "暂无数据");
        return;
    }

    out->status = snapshot->status;

    if (snapshot->status == PET_CLIMATE_STATUS_NO_DATA ||
        snapshot->status == PET_CLIMATE_STATUS_FAULT) {
        snprintf(out->temperature, sizeof(out->temperature), "--");
        snprintf(out->humidity, sizeof(out->humidity), "--");
        snprintf(out->comfort, sizeof(out->comfort), "--");
        snprintf(out->trend, sizeof(out->trend), "—");
    } else {
        format_tenths(out->temperature, sizeof(out->temperature),
                      snapshot->reading.temperature_c, "°C");
        format_tenths(out->humidity, sizeof(out->humidity),
                      snapshot->reading.humidity_pct, "%RH");
        snprintf(out->comfort, sizeof(out->comfort), "%s",
                 comfort_text(snapshot->reading.temperature_c, snapshot->reading.humidity_pct));
        format_trend(out->trend, sizeof(out->trend), snapshot->trend_c_per_10min, "°C");
    }

    switch (snapshot->status) {
    case PET_CLIMATE_STATUS_FAULT:
        snprintf(out->updated, sizeof(out->updated), "传感器异常");
        break;
    case PET_CLIMATE_STATUS_STALE:
        snprintf(out->updated, sizeof(out->updated), "数据过期");
        break;
    case PET_CLIMATE_STATUS_NO_DATA:
        snprintf(out->updated, sizeof(out->updated), "暂无数据");
        break;
    case PET_CLIMATE_STATUS_OK:
    default:
        if (snapshot->age_ms < 1000u) {
            snprintf(out->updated, sizeof(out->updated), "刚刚更新");
        } else {
            snprintf(out->updated, sizeof(out->updated), "%u 秒前更新",
                     (unsigned)(snapshot->age_ms / 1000u));
        }
        break;
    }
}
