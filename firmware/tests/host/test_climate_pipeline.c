#include <assert.h>
#include <stdio.h>

#include "climate_internal.h"

static void test_valid_reading(void)
{
    pet_climate_pipeline_t pipeline;
    pet_climate_pipeline_reset(&pipeline);

    const pet_climate_reading_t reading =
        pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 24.0f, 50.0f, 1000, 0.0f, 0.0f);

    assert(reading.valid);
    assert(reading.error_code == PET_CLIMATE_ERR_NONE);
    assert(reading.quality == 100);
    assert(reading.timestamp_ms == 1000);
    assert(reading.temperature_c > 23.99f && reading.temperature_c < 24.01f);
    assert(pet_climate_pipeline_status(&pipeline, 1000) == PET_CLIMATE_STATUS_OK);
}

static void test_quality_and_stale(void)
{
    pet_climate_pipeline_t pipeline;
    pet_climate_pipeline_reset(&pipeline);

    assert(pet_climate_pipeline_status(&pipeline, 0) == PET_CLIMATE_STATUS_NO_DATA);

    pet_climate_reading_t reading =
        pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 24.0f, 50.0f, 1000, 0.0f, 0.0f);
    assert(reading.valid);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_CHECKSUM, 0.0f, 0.0f, 2000,
                                        0.0f, 0.0f);
    assert(!reading.valid);
    assert(reading.error_code == PET_CLIMATE_ERR_CHECKSUM);
    assert(reading.quality == 60);
    /* 失败时保留上次有效值。 */
    assert(reading.temperature_c > 23.99f && reading.temperature_c < 24.01f);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_TIMEOUT, 0.0f, 0.0f, 3000, 0.0f,
                                        0.0f);
    assert(!reading.valid);
    assert(reading.quality == 40);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 24.2f, 51.0f, 4000, 0.0f,
                                        0.0f);
    assert(reading.valid);
    assert(reading.quality == 50);

    assert(pet_climate_pipeline_status(&pipeline, 4000) == PET_CLIMATE_STATUS_OK);
    assert(pet_climate_pipeline_status(&pipeline, 4000 + PET_CLIMATE_STALE_AFTER_MS) ==
           PET_CLIMATE_STATUS_OK);
    assert(pet_climate_pipeline_status(&pipeline, 4001 + PET_CLIMATE_STALE_AFTER_MS) ==
           PET_CLIMATE_STATUS_STALE);
}

static void test_range_and_spike(void)
{
    pet_climate_pipeline_t pipeline;
    pet_climate_pipeline_reset(&pipeline);

    pet_climate_reading_t reading =
        pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 24.0f, 50.0f, 1000, 0.0f, 0.0f);
    assert(reading.valid);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, -20.0f, 50.0f, 2000, 0.0f,
                                        0.0f);
    assert(!reading.valid);
    assert(reading.error_code == PET_CLIMATE_ERR_RANGE);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 30.0f, 50.0f, 3000, 0.0f,
                                        0.0f);
    assert(!reading.valid);
    assert(reading.error_code == PET_CLIMATE_ERR_SPIKE);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 30.0f, 50.0f, 4000, 0.0f,
                                        0.0f);
    assert(reading.valid);
    assert(reading.temperature_c > 26.0f && reading.temperature_c < 30.01f);
}

static void test_calibration_and_clamp(void)
{
    pet_climate_pipeline_t pipeline;
    pet_climate_pipeline_reset(&pipeline);

    pet_climate_reading_t reading =
        pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 24.0f, 50.0f, 1000, 2.0f, 5.0f);
    assert(reading.valid);
    assert(reading.temperature_c > 25.99f && reading.temperature_c < 26.01f);
    assert(reading.humidity_pct > 54.99f && reading.humidity_pct < 55.01f);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 59.8f, 50.0f, 2000, 5.0f,
                                        5.0f);
    assert(!reading.valid);
    assert(reading.error_code == PET_CLIMATE_ERR_SPIKE);

    reading = pet_climate_pipeline_push(&pipeline, PET_CLIMATE_ERR_NONE, 59.8f, 50.0f, 3000, 5.0f,
                                        5.0f);
    assert(reading.valid);
    assert(reading.temperature_c <= PET_CLIMATE_TEMP_MAX_C);
    assert(reading.humidity_pct <= PET_CLIMATE_HUMIDITY_MAX_PCT);
}

static void test_history(void)
{
    pet_climate_history_t history;
    pet_climate_history_reset(&history);

    const pet_climate_reading_t first = {
        .temperature_c = 20.0f, .humidity_pct = 40.0f, .timestamp_ms = 0, .valid = true};
    const pet_climate_reading_t second = {
        .temperature_c = 22.0f, .humidity_pct = 50.0f, .timestamp_ms = 300000, .valid = true};
    const pet_climate_reading_t third = {
        .temperature_c = 24.0f, .humidity_pct = 60.0f, .timestamp_ms = 600000, .valid = true};

    pet_climate_history_push(&history, &first);
    pet_climate_history_push(&history, &second);
    pet_climate_history_push(&history, &third);

    pet_climate_history_summary_t summary;
    pet_climate_history_summary(&history, 600000, &summary);

    assert(summary.valid);
    assert(summary.temperature_min_c > 19.99f && summary.temperature_min_c < 20.01f);
    assert(summary.temperature_max_c > 23.99f && summary.temperature_max_c < 24.01f);
    assert(summary.temperature_avg_c > 21.99f && summary.temperature_avg_c < 22.01f);
    assert(summary.trend_c_per_10min > 3.99f && summary.trend_c_per_10min < 4.01f);
    assert(summary.trend_rh_per_10min > 19.99f && summary.trend_rh_per_10min < 20.01f);
}

static void test_history_wrap(void)
{
    pet_climate_history_t history;
    pet_climate_history_reset(&history);

    for (uint32_t i = 0; i <= PET_CLIMATE_HISTORY_CAPACITY; i++) {
        const pet_climate_reading_t reading = {
            .temperature_c = 20.0f,
            .humidity_pct = 50.0f,
            .timestamp_ms = i * 5000u,
            .valid = true,
        };
        pet_climate_history_push(&history, &reading);
    }

    assert(history.count == PET_CLIMATE_HISTORY_CAPACITY);

    pet_climate_history_summary_t summary;
    pet_climate_history_summary(&history, PET_CLIMATE_HISTORY_CAPACITY * 5000u, &summary);
    assert(summary.valid);
    assert(summary.temperature_min_c > 19.99f && summary.temperature_max_c < 20.01f);
}

int main(void)
{
    test_valid_reading();
    test_quality_and_stale();
    test_range_and_spike();
    test_calibration_and_clamp();
    test_history();
    test_history_wrap();
    pet_climate_history_t history={0};pet_climate_chart_t chart;
    pet_climate_reading_t point={.temperature_c=22,.humidity_pct=50,.timestamp_ms=60000,.valid=true};
    pet_climate_history_push(&history,&point);
    point.temperature_c=24;point.timestamp_ms=90000;pet_climate_history_push(&history,&point);
    pet_climate_history_chart(&history,119999,&chart);
    assert(chart.points[59].valid && chart.points[59].temperature_centi==2300);
    assert(!chart.points[58].valid);
    pet_climate_history_chart(&history,4000000,&chart);
    assert(!chart.points[59].valid);
    printf("test_climate_pipeline: ok\n");
    return 0;
}
