#include "climate_internal.h"

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static uint8_t clamp_quality(int value)
{
    if (value < 0) {
        return 0;
    }
    if (value > 100) {
        return 100;
    }
    return (uint8_t)value;
}

static float median3(const float *ring, uint8_t count)
{
    float sorted[3];
    for (uint8_t i = 0; i < count; i++) {
        sorted[i] = ring[i];
    }
    for (uint8_t i = 1; i < count; i++) {
        float key = sorted[i];
        int j = (int)i - 1;
        while (j >= 0 && sorted[j] > key) {
            sorted[j + 1] = sorted[j];
            j--;
        }
        sorted[j + 1] = key;
    }
    return sorted[count / 2];
}

static float average_of(const float *ring, uint8_t count)
{
    if (count == 0) {
        return 0.0f;
    }
    float sum = 0.0f;
    for (uint8_t i = 0; i < count; i++) {
        sum += ring[i];
    }
    return sum / (float)count;
}

static void ring_push(float *ring, uint8_t capacity, uint8_t *count, uint8_t *head, float value)
{
    ring[*head] = value;
    *head = (uint8_t)((*head + 1u) % capacity);
    if (*count < capacity) {
        (*count)++;
    }
}

static uint8_t error_penalty(pet_climate_error_t error)
{
    switch (error) {
    case PET_CLIMATE_ERR_CHECKSUM:
        return 40;
    case PET_CLIMATE_ERR_RANGE:
        return 60;
    case PET_CLIMATE_ERR_TIMEOUT:
        return 20;
    case PET_CLIMATE_ERR_SPIKE:
        return 10;
    default:
        return 20;
    }
}

void pet_climate_pipeline_reset(pet_climate_pipeline_t *pipeline)
{
    if (pipeline == NULL) {
        return;
    }
    *pipeline = (pet_climate_pipeline_t){0};
    pipeline->quality = 100;
}

static pet_climate_reading_t invalid_reading(const pet_climate_pipeline_t *pipeline,
                                             pet_climate_error_t error, uint32_t now_ms)
{
    pet_climate_reading_t reading = {
        .timestamp_ms = now_ms,
        .error_code = (uint16_t)error,
        .quality = pipeline->quality,
        .valid = false,
    };
    if (pipeline->has_valid) {
        reading.temperature_c = pipeline->last_temperature_c;
        reading.humidity_pct = pipeline->last_humidity_pct;
    }
    return reading;
}

pet_climate_reading_t pet_climate_pipeline_push(pet_climate_pipeline_t *pipeline,
                                                pet_climate_error_t error,
                                                float temperature_c,
                                                float humidity_pct,
                                                uint32_t now_ms,
                                                float temperature_offset_c,
                                                float humidity_offset_pct)
{
    if (pipeline == NULL) {
        pet_climate_reading_t empty = {0};
        empty.error_code = (uint16_t)PET_CLIMATE_ERR_RANGE;
        return empty;
    }

    if (error == PET_CLIMATE_ERR_NONE &&
        (temperature_c < PET_CLIMATE_TEMP_MIN_C || temperature_c > PET_CLIMATE_TEMP_MAX_C ||
         humidity_pct < PET_CLIMATE_HUMIDITY_MIN_PCT || humidity_pct > PET_CLIMATE_HUMIDITY_MAX_PCT)) {
        error = PET_CLIMATE_ERR_RANGE;
    }

    if (error != PET_CLIMATE_ERR_NONE) {
        pipeline->quality = clamp_quality((int)pipeline->quality - error_penalty(error));
        return invalid_reading(pipeline, error, now_ms);
    }

    const float temp_delta = temperature_c - pipeline->last_temperature_c;
    const float humidity_delta = humidity_pct - pipeline->last_humidity_pct;
    const bool spike = pipeline->has_valid &&
                       (temp_delta > PET_CLIMATE_SPIKE_TEMP_C ||
                        temp_delta < -PET_CLIMATE_SPIKE_TEMP_C ||
                        humidity_delta > PET_CLIMATE_SPIKE_HUMIDITY_PCT ||
                        humidity_delta < -PET_CLIMATE_SPIKE_HUMIDITY_PCT);

    if (spike) {
        if (pipeline->spike_streak == 0) {
            pipeline->spike_streak = 1;
            pipeline->quality = clamp_quality((int)pipeline->quality - error_penalty(PET_CLIMATE_ERR_SPIKE));
            return invalid_reading(pipeline, PET_CLIMATE_ERR_SPIKE, now_ms);
        }
        /* 连续两次同样跳变视为真实变化，接受本次读数。 */
        pipeline->spike_streak = 0;
    } else {
        pipeline->spike_streak = 0;
    }

    ring_push(pipeline->temperature_median, 3, &pipeline->temperature_median_count,
              &pipeline->temperature_median_head, temperature_c);
    ring_push(pipeline->humidity_median, 3, &pipeline->humidity_median_count,
              &pipeline->humidity_median_head, humidity_pct);

    const float temperature_median =
        median3(pipeline->temperature_median, pipeline->temperature_median_count);
    const float humidity_median =
        median3(pipeline->humidity_median, pipeline->humidity_median_count);

    ring_push(pipeline->temperature_average, 5, &pipeline->temperature_average_count,
              &pipeline->temperature_average_head, temperature_median);
    ring_push(pipeline->humidity_average, 5, &pipeline->humidity_average_count,
              &pipeline->humidity_average_head, humidity_median);

    pipeline->last_temperature_c =
        clamp_float(average_of(pipeline->temperature_average, pipeline->temperature_average_count) +
                        temperature_offset_c,
                    PET_CLIMATE_TEMP_MIN_C, PET_CLIMATE_TEMP_MAX_C);
    pipeline->last_humidity_pct =
        clamp_float(average_of(pipeline->humidity_average, pipeline->humidity_average_count) +
                        humidity_offset_pct,
                    PET_CLIMATE_HUMIDITY_MIN_PCT, PET_CLIMATE_HUMIDITY_MAX_PCT);
    pipeline->last_valid_ms = now_ms;
    pipeline->has_valid = true;
    pipeline->quality = clamp_quality((int)pipeline->quality + 10);

    pet_climate_reading_t reading = {
        .temperature_c = pipeline->last_temperature_c,
        .humidity_pct = pipeline->last_humidity_pct,
        .timestamp_ms = now_ms,
        .error_code = (uint16_t)PET_CLIMATE_ERR_NONE,
        .quality = pipeline->quality,
        .valid = true,
    };
    return reading;
}

pet_climate_status_t pet_climate_pipeline_status(const pet_climate_pipeline_t *pipeline,
                                                 uint32_t now_ms)
{
    if (pipeline == NULL || !pipeline->has_valid) {
        return PET_CLIMATE_STATUS_NO_DATA;
    }
    if (now_ms >= pipeline->last_valid_ms &&
        now_ms - pipeline->last_valid_ms > PET_CLIMATE_STALE_AFTER_MS) {
        return PET_CLIMATE_STATUS_STALE;
    }
    return PET_CLIMATE_STATUS_OK;
}

void pet_climate_history_reset(pet_climate_history_t *history)
{
    if (history == NULL) {
        return;
    }
    history->head = 0;
    history->count = 0;
}

static int16_t to_centi(float value)
{
    return (int16_t)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
}

void pet_climate_history_push(pet_climate_history_t *history, const pet_climate_reading_t *reading)
{
    if (history == NULL || reading == NULL || !reading->valid) {
        return;
    }

    history->points[history->head] = (pet_climate_history_point_t){
        .temperature_centi = to_centi(reading->temperature_c),
        .humidity_centi = to_centi(reading->humidity_pct),
        .timestamp_ms = reading->timestamp_ms,
    };
    history->head = (uint16_t)((history->head + 1u) % PET_CLIMATE_HISTORY_CAPACITY);
    if (history->count < PET_CLIMATE_HISTORY_CAPACITY) {
        history->count++;
    }
}

void pet_climate_history_summary(const pet_climate_history_t *history, uint32_t now_ms,
                                 pet_climate_history_summary_t *out)
{
    if (out == NULL) {
        return;
    }
    *out = (pet_climate_history_summary_t){0};
    if (history == NULL || history->count == 0) {
        return;
    }

    const uint32_t window_start =
        now_ms > PET_CLIMATE_HISTORY_WINDOW_MS ? now_ms - PET_CLIMATE_HISTORY_WINDOW_MS : 0;
    const uint32_t trend_start =
        now_ms > PET_CLIMATE_TREND_WINDOW_MS ? now_ms - PET_CLIMATE_TREND_WINDOW_MS : 0;

    const uint16_t oldest = (uint16_t)((history->head + PET_CLIMATE_HISTORY_CAPACITY - history->count) %
                                       PET_CLIMATE_HISTORY_CAPACITY);

    float temp_sum = 0.0f;
    float hum_sum = 0.0f;
    uint16_t samples = 0;
    int16_t temp_min = INT16_MAX;
    int16_t temp_max = INT16_MIN;
    int16_t hum_min = INT16_MAX;
    int16_t hum_max = INT16_MIN;
    bool trend_point_found = false;
    int16_t trend_temp_start = 0;
    int16_t trend_hum_start = 0;

    for (uint16_t i = 0; i < history->count; i++) {
        const pet_climate_history_point_t *point =
            &history->points[(oldest + i) % PET_CLIMATE_HISTORY_CAPACITY];
        if (point->timestamp_ms < window_start) {
            continue;
        }

        samples++;
        temp_sum += (float)point->temperature_centi;
        hum_sum += (float)point->humidity_centi;
        if (point->temperature_centi < temp_min) {
            temp_min = point->temperature_centi;
        }
        if (point->temperature_centi > temp_max) {
            temp_max = point->temperature_centi;
        }
        if (point->humidity_centi < hum_min) {
            hum_min = point->humidity_centi;
        }
        if (point->humidity_centi > hum_max) {
            hum_max = point->humidity_centi;
        }

        if (!trend_point_found && point->timestamp_ms >= trend_start) {
            trend_point_found = true;
            trend_temp_start = point->temperature_centi;
            trend_hum_start = point->humidity_centi;
        }
    }

    if (samples == 0) {
        return;
    }

    const pet_climate_history_point_t *newest =
        &history->points[(history->head + PET_CLIMATE_HISTORY_CAPACITY - 1u) %
                         PET_CLIMATE_HISTORY_CAPACITY];

    out->valid = true;
    out->temperature_min_c = (float)temp_min / 100.0f;
    out->temperature_max_c = (float)temp_max / 100.0f;
    out->temperature_avg_c = temp_sum / (float)samples / 100.0f;
    out->humidity_min_pct = (float)hum_min / 100.0f;
    out->humidity_max_pct = (float)hum_max / 100.0f;
    out->humidity_avg_pct = hum_sum / (float)samples / 100.0f;

    if (trend_point_found && newest->timestamp_ms > trend_start) {
        out->trend_c_per_10min = (float)(newest->temperature_centi - trend_temp_start) / 100.0f;
        out->trend_rh_per_10min = (float)(newest->humidity_centi - trend_hum_start) / 100.0f;
    }
}
