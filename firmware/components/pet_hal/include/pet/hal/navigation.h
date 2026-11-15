#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { PET_NAV_NONE, PET_NAV_UP, PET_NAV_DOWN, PET_NAV_LEFT, PET_NAV_RIGHT } pet_navigation_t;
typedef struct {
    uint16_t x, y; /* 12-bit ADC1 values; center is approximately 2048. */
    bool pressed, valid;
} pet_navigation_sample_t;
typedef struct {
    uint32_t neutral_ms, changed_ms, repeated_ms;
    pet_navigation_t raw, stable;
    bool neutral_pending, armed, repeating;
} pet_navigation_filter_t;

/* Single UI owner. Startup/fault/preemption requires 100 ms released neutral;
 * directions debounce for 30 ms, only up/down repeat (400 ms, then 160 ms). */
pet_navigation_t pet_navigation_update(pet_navigation_filter_t *filter,
    const pet_navigation_sample_t *sample, bool blocked, uint32_t now);
