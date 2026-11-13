#include <assert.h>
#include <stdio.h>
#include "pet/presentation/pet_motion.h"

int main(void)
{
    pet_motion_t motion = {0};
    pet_motion_pose_t pose;
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, 0, true, &pose));
    assert(pose.state == PET_GROWTH_IDLE);
    assert(!pet_motion_step(&motion, PET_GROWTH_IDLE, 39, true, &pose));
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, 40, true, &pose));
    assert(pet_motion_step(&motion, PET_GROWTH_HUNGRY, 41, true, &pose));
    assert(pose.state == PET_GROWTH_HUNGRY); /* 更高优先级不等下一帧。 */
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, 100, true, &pose));
    assert(pose.state == PET_GROWTH_HUNGRY);
    pet_motion_step(&motion, PET_GROWTH_HUNGRY, 1000, true, &pose);
    pet_motion_step(&motion, PET_GROWTH_IDLE, 2000, true, &pose);
    pet_motion_step(&motion, PET_GROWTH_IDLE, 4999, true, &pose);
    assert(pose.state == PET_GROWTH_HUNGRY);
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, 5000, true, &pose));
    assert(pose.state == PET_GROWTH_IDLE);
    assert(!pet_motion_step(&motion, PET_GROWTH_IDLE, 5040, false, &pose));
    assert(!pet_motion_step(&motion, PET_GROWTH_HAPPY, 9000, false, &pose));
    assert(pet_motion_step(&motion, PET_GROWTH_SLEEPING, 10000, true, &pose));
    assert(pose.state == PET_GROWTH_SLEEPING);
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, 10001, true, &pose));
    assert(pose.state == PET_GROWTH_IDLE); /* 唤醒和瞬态结束不滞留三秒。 */
    pet_motion_step(&motion, PET_GROWTH_EXCITED, 11000, true, &pose);
    pet_motion_step(&motion, PET_GROWTH_IDLE, 14000, true, &pose);
    assert(pose.state == PET_GROWTH_IDLE);

    /* 单调毫秒时钟约 49 天回绕，跨边界仍保持帧间隔和连续稳定窗口。 */
    motion = (pet_motion_t){0};
    const uint32_t start = UINT32_MAX - 20u;
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, start, true, &pose));
    assert(!pet_motion_step(&motion, PET_GROWTH_IDLE, start + 39u, true, &pose));
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, start + 40u, true, &pose));
    assert(pet_motion_step(&motion, PET_GROWTH_HUNGRY, start + 41u, true, &pose));

    motion = (pet_motion_t){0};
    const uint32_t settling = UINT32_MAX - 1000u;
    pet_motion_step(&motion, PET_GROWTH_HUNGRY, settling, true, &pose);
    pet_motion_step(&motion, PET_GROWTH_IDLE, settling + 40u, true, &pose);
    pet_motion_step(&motion, PET_GROWTH_IDLE, settling + 3039u, true, &pose);
    assert(pose.state == PET_GROWTH_HUNGRY);
    assert(pet_motion_step(&motion, PET_GROWTH_IDLE, settling + 3040u, true, &pose));
    assert(pose.state == PET_GROWTH_IDLE);

    for (unsigned state = 0; state <= PET_GROWTH_SLEEPING; ++state) {
        motion = (pet_motion_t){0};
        for (uint32_t elapsed = 0; elapsed < 86400000u; elapsed += 7919u) {
            assert(pet_motion_step(&motion, (pet_growth_state_t)state, start + elapsed, true, &pose));
            assert(pose.state == (pet_growth_state_t)state && pose.entry_pct <= 100);
            assert(pose.lift >= -2 && pose.lift <= 8);
            assert(pose.sway >= -3 && pose.sway <= 3);
            assert(pose.breath >= -2 && pose.breath <= 2);
            assert(pose.gaze >= -2 && pose.gaze <= 2);
            assert(pose.gesture >= -3 && pose.gesture <= 3);
        }
    }
    puts("pet motion: cadence, priority, stabilization, suspend, wake, wrap and pose bounds passed");
}
