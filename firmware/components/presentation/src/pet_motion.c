#include "pet/presentation/pet_motion.h"

#include <stddef.h>

static unsigned priority(pet_growth_state_t state)
{
    static const uint8_t values[] = {0, 1, 2, 3, 4, 5, 7, 6};
    return values[state];
}

/* 三角波只做整数插值，避免在每帧引入浮点三角函数。 */
static int wave(uint32_t elapsed, unsigned period, int amplitude)
{
    unsigned phase = elapsed % period;
    unsigned half = period / 2;
    unsigned value = phase < half ? phase : period - phase;
    return (int)(value * (unsigned)(2 * amplitude) / half) - amplitude;
}

bool pet_motion_step(pet_motion_t *m, pet_growth_state_t state, uint32_t now,
                      bool visible, pet_motion_pose_t *pose)
{
    if (!m || !pose) return false;
    if (!visible) {
        m->running = false;
        m->candidate_pending = false;
        return false;
    }
    if ((unsigned)state > PET_GROWTH_SLEEPING) state = PET_GROWTH_IDLE;
    bool changed = !m->running;
    if (!m->running) {
        m->state = state;
        m->running = true;
        m->candidate_pending = false;
    } else if (state == m->state) {
        m->candidate_pending = false;
    } else if (priority(state) > priority(m->state) || state == PET_GROWTH_SLEEPING ||
               m->state == PET_GROWTH_SLEEPING || m->state == PET_GROWTH_EXCITED) {
        changed = true;
    } else {
        if (!m->candidate_pending || m->candidate != state) {
            m->candidate = state;
            m->candidate_ms = now;
            m->candidate_pending = true;
        }
        changed = now - m->candidate_ms >= 3000;
    }
    if (changed) {
        m->state = state;
        m->started_ms = now;
        m->candidate_pending = false;
    }
    if (!changed && now - m->frame_ms < 40) return false;
    m->frame_ms = now;
    uint32_t elapsed = now - m->started_ms;
    *pose = (pet_motion_pose_t){
        .state = m->state,
        .entry_pct = elapsed >= 100 ? 100 : (uint8_t)elapsed,
        .breath = (int8_t)wave(elapsed, m->state == PET_GROWTH_SLEEPING ? 4000 : 2400, 2),
        .gaze = (int8_t)wave(elapsed, 6400, 2),
        .gesture = (int8_t)wave(elapsed, 1400, 3),
        .blink = elapsed % 4200 >= 4000,
    };
    switch (m->state) {
    case PET_GROWTH_HAPPY:
        pose->sway = (int8_t)wave(elapsed, 1600, 3);
        break;
    case PET_GROWTH_EXCITED:
        pose->lift = (int8_t)(wave(elapsed, 700, 4) + 4);
        pose->sway = (int8_t)wave(elapsed, 1000, 3);
        break;
    case PET_GROWTH_SAD:
    case PET_GROWTH_SICK:
        pose->sway = (int8_t)wave(elapsed, 4800, 1);
        break;
    case PET_GROWTH_HUNGRY:
        pose->lift = (int8_t)(wave(elapsed, 2200, 2) + 2);
        pose->gaze = -2;
        break;
    case PET_GROWTH_SLEEPY:
        pose->lift = (int8_t)wave(elapsed, 3600, 2);
        break;
    default:
        break;
    }
    return true;
}
