#include "pet/hal/ambient_light.h"

uint8_t pet_ambient_brightness(pet_ambient_filter_t *p, uint16_t raw,
    bool valid, uint32_t now, uint8_t ceiling)
{
    if (ceiling > 100) ceiling = 100;
    if (!p) return ceiling;
    if (!ceiling || !valid || raw > 4095) {
        *p = (pet_ambient_filter_t){0};
        return ceiling;
    }
    if (p->initialized && p->applied > ceiling) p->applied = ceiling;
    if (p->initialized && now - p->updated_ms < 500) return p->applied;
    p->filtered = p->initialized ? (3u * p->filtered + raw) / 4u : raw;
    unsigned target = 15u + (85u * p->filtered) / 4095u;
    if (target > ceiling) target = ceiling;
    const bool boundary = target == 15 || target == ceiling;
    if (!p->initialized) p->applied = target;
    else if (target > p->applied && (boundary || target > p->applied + 1u))
        p->applied += target - p->applied > 5u ? 5u : target - p->applied;
    else if (target < p->applied && (boundary || target + 1u < p->applied))
        p->applied -= p->applied - target > 5u ? 5u : p->applied - target;
    p->initialized = true;
    p->updated_ms = now;
    return p->applied;
}
