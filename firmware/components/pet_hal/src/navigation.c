#include "pet/hal/navigation.h"
#include <stdlib.h>

pet_navigation_t pet_navigation_update(pet_navigation_filter_t *p,
    const pet_navigation_sample_t *sample, bool blocked, uint32_t now)
{
    if (!p) return PET_NAV_NONE;
    if (!sample || !sample->valid || sample->x > 4095 || sample->y > 4095 || blocked) {
        *p = (pet_navigation_filter_t){0};
        return PET_NAV_NONE;
    }
    const int x = (int)sample->x - 2048, y = (int)sample->y - 2048;
    const bool neutral = abs(x) <= 450 && abs(y) <= 450;
    if (!p->armed) {
        if (!neutral || sample->pressed) p->neutral_pending = false;
        else if (!p->neutral_pending) { p->neutral_pending = true; p->neutral_ms = now; }
        else if (now - p->neutral_ms >= 100) p->armed = true;
        return PET_NAV_NONE;
    }
    pet_navigation_t direction = PET_NAV_NONE;
    if (!sample->pressed && !neutral) {
        /* Enter outside the dead zone; retain an existing direction down to
         * the release threshold so ADC noise cannot cause duplicate pushes. */
        if (abs(x) > abs(y) && abs(x) >= 700) direction = x < 0 ? PET_NAV_LEFT : PET_NAV_RIGHT;
        else if (abs(y) >= 700) direction = y < 0 ? PET_NAV_UP : PET_NAV_DOWN;
        else if ((p->stable == PET_NAV_LEFT && x < -450) ||
                 (p->stable == PET_NAV_RIGHT && x > 450) ||
                 (p->stable == PET_NAV_UP && y < -450) ||
                 (p->stable == PET_NAV_DOWN && y > 450)) direction = p->stable;
    }
    if (direction != p->raw) { p->raw = direction; p->changed_ms = now; }
    if (p->raw != p->stable && now - p->changed_ms >= 30) {
        p->stable = p->raw;
        p->repeated_ms = now;
        p->repeating = false;
        return p->stable;
    }
    if ((p->stable == PET_NAV_UP || p->stable == PET_NAV_DOWN) && direction == p->stable &&
        now - p->repeated_ms >= (p->repeating ? 160u : 400u)) {
        p->repeating = true;
        p->repeated_ms = now;
        return p->stable;
    }
    return PET_NAV_NONE;
}
