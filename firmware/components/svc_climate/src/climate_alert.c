#include "climate_internal.h"

void pet_climate_alert_reset(pet_climate_alert_state_t *state)
{
    if (state == NULL) {
        return;
    }
    *state = (pet_climate_alert_state_t){0};
}

static pet_climate_alert_t evaluate(const pet_climate_alert_state_t *state,
                                    float temperature_c, float humidity_pct)
{
    if (humidity_pct >= 95.0f && state->has_previous &&
        temperature_c <= state->previous_temperature_c - 1.0f) {
        return PET_CLIMATE_ALERT_CONDENSATION;
    }
    if (temperature_c > 30.0f) {
        return PET_CLIMATE_ALERT_HOT;
    }
    if (temperature_c < 10.0f) {
        return PET_CLIMATE_ALERT_COLD;
    }
    if (humidity_pct > 80.0f) {
        return PET_CLIMATE_ALERT_HUMID;
    }
    if (humidity_pct < 30.0f) {
        return PET_CLIMATE_ALERT_DRY;
    }
    return PET_CLIMATE_ALERT_NONE;
}

pet_climate_alert_t pet_climate_alert_update(pet_climate_alert_state_t *state,
                                             bool valid, float temperature_c,
                                             float humidity_pct, uint32_t now_ms)
{
    if (state == NULL) {
        return PET_CLIMATE_ALERT_NONE;
    }

    /* 无效读数不参与判定，也不计入恢复计数。 */
    if (!valid) {
        return PET_CLIMATE_ALERT_NONE;
    }

    const pet_climate_alert_t candidate = evaluate(state, temperature_c, humidity_pct);

    state->previous_temperature_c = temperature_c;
    state->has_previous = true;

    if (state->active == PET_CLIMATE_ALERT_NONE) {
        state->normal_streak = 0;
        if (candidate == PET_CLIMATE_ALERT_NONE) {
            return PET_CLIMATE_ALERT_NONE;
        }
        state->active = candidate;
        state->last_emit_ms = now_ms;
        return candidate;
    }

    if (candidate == PET_CLIMATE_ALERT_NONE) {
        if (state->normal_streak < PET_CLIMATE_ALERT_RECOVERY_COUNT) {
            state->normal_streak++;
        }
        if (state->normal_streak >= PET_CLIMATE_ALERT_RECOVERY_COUNT) {
            state->active = PET_CLIMATE_ALERT_NONE;
            state->last_emit_ms = 0;
        }
        return PET_CLIMATE_ALERT_NONE;
    }

    state->normal_streak = 0;

    const bool debounce_elapsed = now_ms >= state->last_emit_ms &&
                                  now_ms - state->last_emit_ms >= PET_CLIMATE_ALERT_DEBOUNCE_MS;
    if (!debounce_elapsed) {
        return PET_CLIMATE_ALERT_NONE;
    }

    /* 同一告警作为提醒重发；不同告警在冷却结束后切换。 */
    state->active = candidate;
    state->last_emit_ms = now_ms;
    return candidate;
}
