#include <assert.h>
#include <stdio.h>

#include "climate_internal.h"

static void test_debounce(void)
{
    pet_climate_alert_state_t state;
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 0) == PET_CLIMATE_ALERT_HOT);
    assert(state.active == PET_CLIMATE_ALERT_HOT);
    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 60000) == PET_CLIMATE_ALERT_NONE);
    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 600000) == PET_CLIMATE_ALERT_HOT);
}

static void test_recovery(void)
{
    pet_climate_alert_state_t state;
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 0) == PET_CLIMATE_ALERT_HOT);
    assert(pet_climate_alert_update(&state, true, 25.0f, 50.0f, 1000) == PET_CLIMATE_ALERT_NONE);
    assert(pet_climate_alert_update(&state, true, 25.0f, 50.0f, 2000) == PET_CLIMATE_ALERT_NONE);
    assert(state.active == PET_CLIMATE_ALERT_HOT);
    assert(pet_climate_alert_update(&state, true, 25.0f, 50.0f, 3000) == PET_CLIMATE_ALERT_NONE);
    assert(state.active == PET_CLIMATE_ALERT_NONE);

    /* 恢复后冷却清零，可立即再次告警。 */
    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 4000) == PET_CLIMATE_ALERT_HOT);
}

static void test_invalid_does_not_recover(void)
{
    pet_climate_alert_state_t state;
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 0) == PET_CLIMATE_ALERT_HOT);
    for (uint32_t i = 1; i <= 5; i++) {
        assert(pet_climate_alert_update(&state, false, 0.0f, 0.0f, i * 1000) ==
               PET_CLIMATE_ALERT_NONE);
    }
    assert(state.active == PET_CLIMATE_ALERT_HOT);
}

static void test_switch_waits_for_debounce(void)
{
    pet_climate_alert_state_t state;
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 35.0f, 50.0f, 0) == PET_CLIMATE_ALERT_HOT);
    assert(pet_climate_alert_update(&state, true, 5.0f, 50.0f, 1000) == PET_CLIMATE_ALERT_NONE);
    assert(state.active == PET_CLIMATE_ALERT_HOT);
    assert(pet_climate_alert_update(&state, true, 5.0f, 50.0f, 600000) == PET_CLIMATE_ALERT_COLD);
    assert(state.active == PET_CLIMATE_ALERT_COLD);
}

static void test_dry_humid_condensation(void)
{
    pet_climate_alert_state_t state;
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 25.0f, 20.0f, 0) == PET_CLIMATE_ALERT_DRY);
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 25.0f, 85.0f, 0) == PET_CLIMATE_ALERT_HUMID);
    pet_climate_alert_reset(&state);

    assert(pet_climate_alert_update(&state, true, 25.0f, 60.0f, 0) == PET_CLIMATE_ALERT_NONE);
    assert(pet_climate_alert_update(&state, true, 23.9f, 95.0f, 1000) ==
           PET_CLIMATE_ALERT_CONDENSATION);
}

int main(void)
{
    test_debounce();
    test_recovery();
    test_invalid_does_not_recover();
    test_switch_waits_for_debounce();
    test_dry_humid_condensation();
    printf("test_climate_alert: ok\n");
    return 0;
}
