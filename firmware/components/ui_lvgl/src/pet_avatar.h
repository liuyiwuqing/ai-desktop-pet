#pragma once

#include "lvgl.h"
#include "pet/presentation/pet_motion.h"

typedef struct {
    lv_obj_t *object;
    pet_motion_t motion;
    pet_motion_pose_t pose;
    pet_growth_environment_t environment;
    bool speaking; uint8_t speech_level;
} pet_avatar_t;

/* 图元绘制，不载入位图或创建周期任务；由拥有 LVGL 的任务显式推进。 */
void pet_avatar_init(pet_avatar_t *avatar, lv_obj_t *parent);
void pet_avatar_set_environment(pet_avatar_t *avatar, pet_growth_environment_t environment);
bool pet_avatar_tick(pet_avatar_t *avatar, pet_growth_state_t state, uint32_t now_ms,
                     bool visible);

void pet_avatar_set_speech(pet_avatar_t *avatar,bool speaking,uint8_t level);
