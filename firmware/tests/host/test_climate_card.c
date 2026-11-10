#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pet/presentation/climate_card.h"

static pet_climate_snapshot_t snapshot(pet_climate_status_t status)
{
    pet_climate_snapshot_t value = {
        .reading = {
            .temperature_c = 24.6f,
            .humidity_pct = 58.0f,
            .quality = 90,
            .valid = true,
        },
        .status = status,
        .trend_c_per_10min = 0.3f,
        .trend_rh_per_10min = 1.0f,
        .age_ms = 5000,
    };
    return value;
}

static void test_ok_card(void)
{
    const pet_climate_snapshot_t value = snapshot(PET_CLIMATE_STATUS_OK);
    pet_climate_card_t card;
    pet_climate_card_render(&value, &card);

    assert(strcmp(card.temperature, "24.6 °C") == 0);
    assert(strcmp(card.humidity, "58.0 %RH") == 0);
    assert(strcmp(card.comfort, "舒适") == 0);
    assert(strcmp(card.trend, "↑ 0.3 °C") == 0);
    assert(strcmp(card.updated, "5 秒前更新") == 0);
    assert(card.status == PET_CLIMATE_STATUS_OK);
}

static void test_stale_keeps_values(void)
{
    const pet_climate_snapshot_t value = snapshot(PET_CLIMATE_STATUS_STALE);
    pet_climate_card_t card;
    pet_climate_card_render(&value, &card);

    assert(strcmp(card.temperature, "24.6 °C") == 0);
    assert(strcmp(card.updated, "数据过期") == 0);
}

static void test_fault_and_no_data(void)
{
    pet_climate_card_t card;
    const pet_climate_snapshot_t fault = snapshot(PET_CLIMATE_STATUS_FAULT);
    pet_climate_card_render(&fault, &card);
    assert(strcmp(card.temperature, "--") == 0);
    assert(strcmp(card.humidity, "--") == 0);
    assert(strcmp(card.updated, "传感器异常") == 0);

    pet_climate_card_render(NULL, &card);
    assert(strcmp(card.temperature, "--") == 0);
    assert(strcmp(card.updated, "暂无数据") == 0);
}

static void test_negative_trend_and_hot(void)
{
    pet_climate_snapshot_t value = snapshot(PET_CLIMATE_STATUS_OK);
    value.trend_c_per_10min = -0.25f;
    value.reading.temperature_c = 32.4f;
    value.reading.humidity_pct = 45.0f;

    pet_climate_card_t card;
    pet_climate_card_render(&value, &card);

    assert(strcmp(card.trend, "↓ 0.3 °C") == 0);
    assert(strcmp(card.temperature, "32.4 °C") == 0);
    assert(strcmp(card.comfort, "偏热") == 0);
}

int main(void)
{
    test_ok_card();
    test_stale_keeps_values();
    test_fault_and_no_data();
    test_negative_trend_and_hot();
    printf("test_climate_card: ok\n");
    return 0;
}
