#pragma once

#include "pet/growth.h"

/* UI 任务拥有的表现状态，不写成长模型或存档。时间差使用无符号减法跨越毫秒回绕。 */
typedef struct {
    pet_growth_state_t state, candidate;
    uint32_t started_ms, candidate_ms, frame_ms;
    bool running, candidate_pending;
} pet_motion_t;

typedef struct {
    pet_growth_state_t state;
    int8_t lift, sway, breath, gaze, gesture;
    uint8_t entry_pct;
    bool blink;
} pet_motion_pose_t;

/* visible=false 时停帧，恢复后立即展示当前业务状态；最多 25 fps，状态切换可立即绘制。
 * 较低优先级需连续稳定 3 s，睡眠/唤醒及升级瞬态结束立即切换。 */
bool pet_motion_step(pet_motion_t *motion, pet_growth_state_t state, uint32_t now_ms,
                      bool visible, pet_motion_pose_t *pose);
