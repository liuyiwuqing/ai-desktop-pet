#pragma once

#include "lvgl.h"
#include "pet/companion.h"
#include "pet/presentation/pet_motion.h"

typedef enum {
    PET_REACT_NONE, PET_REACT_ATTEND, PET_REACT_FEED, PET_REACT_PET, PET_REACT_CHEER,
    PET_REACT_LISTEN, PET_REACT_THINK, PET_REACT_SPEAK, PET_REACT_SLEEP, PET_REACT_WAKE,
    PET_REACT_FOCUS, PET_REACT_FAILURE, PET_REACT_WAVE
} pet_reaction_t;
typedef struct {
    lv_obj_t *object;
    pet_motion_t motion;
    pet_motion_pose_t pose;
    pet_growth_environment_t environment;
    pet_appearance_t appearance;
    pet_reaction_t reaction;
    uint32_t reaction_ms, now_ms;
    bool cabin;
    bool speaking; uint8_t speech_level;
} pet_avatar_t;

/* 图元绘制，不载入位图或创建周期任务；由拥有 LVGL 的任务显式推进。 */
void pet_avatar_init(pet_avatar_t *avatar, lv_obj_t *parent);
void pet_avatar_set_environment(pet_avatar_t *avatar, pet_growth_environment_t environment);
bool pet_avatar_tick(pet_avatar_t *avatar, pet_growth_state_t state, uint32_t now_ms,
                     bool visible);

void pet_avatar_set_speech(pet_avatar_t *avatar,bool speaking,uint8_t level);

void pet_avatar_set_appearance(pet_avatar_t *avatar, const pet_appearance_t *appearance, bool cabin);
void pet_avatar_react(pet_avatar_t *avatar, pet_reaction_t reaction, uint32_t now_ms);
