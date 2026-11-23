#include "pet/hal/keys.h"

#include <string.h>

static unsigned emit(pet_key_event_t *out, unsigned capacity, unsigned produced, uint8_t kind, uint8_t key)
{
    if (produced < capacity) out[produced] = (pet_key_event_t){.kind = kind, .key = key};
    return produced + 1;
}

static uint8_t combo_kind(uint8_t mask)
{
    if (mask == (uint8_t)((1u << PET_KEY_CONFIRM) | (1u << PET_KEY_NEXT)))
        return PET_KEY_EVENT_COMBO_CONFIRM_NEXT;
    if (mask == (uint8_t)((1u << PET_KEY_NEXT) | (1u << PET_KEY_VOICE)))
        return PET_KEY_EVENT_COMBO_NEXT_VOICE;
    return PET_KEY_EVENT_NONE;
}

void pet_key_tracker_init(pet_key_tracker_t *tracker)
{
    if (tracker) memset(tracker, 0, sizeof(*tracker));
}

unsigned pet_key_tracker_update(pet_key_tracker_t *t, const bool pressed[PET_KEY_COUNT],
    uint32_t now, pet_key_event_t *out, unsigned capacity)
{
    if (!t || !pressed) return 0;
    unsigned produced = 0;
    for (unsigned key = 0; key < PET_KEY_COUNT; ++key) {
        const bool raw = pressed[key];
        if (raw != t->sample[key]) { /* 原始电平变化：重新开始 30ms 消抖。 */
            t->sample[key] = raw;
            t->sample_ms[key] = now;
            continue;
        }
        if (raw == t->stable[key]) {
            if (raw && !(t->combo_keys & (1u << key)) && !t->long_fired[key] &&
                now - t->press_ms[key] >= PET_KEY_LONG_PRESS_MS) {
                t->long_fired[key] = true;
                produced = emit(out, capacity, produced, PET_KEY_EVENT_LONG_PRESS, (uint8_t)key);
            }
            continue;
        }
        if (now - t->sample_ms[key] < PET_KEY_DEBOUNCE_MS) continue;
        t->stable[key] = raw;
        if (raw) { /* 稳定按下沿 */
            t->press_ms[key] = now;
            t->long_fired[key] = false;
            if (!t->combo_keys) {
                for (unsigned other = 0; other < PET_KEY_COUNT && !t->combo_keys; ++other) {
                    if (other == key || !t->stable[other] || t->long_fired[other]) continue;
                    if (now - t->press_ms[other] > PET_KEY_COMBO_WINDOW_MS) continue;
                    const uint8_t mask = (uint8_t)((1u << key) | (1u << other));
                    if (combo_kind(mask) != PET_KEY_EVENT_NONE) t->combo_keys = mask;
                }
            }
        } else { /* 稳定释放沿 */
            if (t->combo_keys & (1u << key)) {
                bool all_released = true;
                for (unsigned k = 0; k < PET_KEY_COUNT && all_released; ++k)
                    if ((t->combo_keys & (1u << k)) && t->stable[k]) all_released = false;
                if (all_released) {
                    produced = emit(out, capacity, produced, combo_kind(t->combo_keys), 0);
                    t->combo_keys = 0;
                }
            } else if (!t->long_fired[key]) {
                produced = emit(out, capacity, produced, PET_KEY_EVENT_SHORT_PRESS, (uint8_t)key);
            }
        }
    }
    return produced;
}
