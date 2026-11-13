#pragma once

#include "pet/growth.h"

#define PET_GROWTH_UNITS 3600
#define PET_GROWTH_VALUE_MAX (100 * PET_GROWTH_UNITS)
#define PET_GROWTH_OFFLINE_MAX_S (8 * 60 * 60)

/* 一点 = 3600 单位，每秒按整数结算；短间隔与睡眠切换不会丢失衰减小数。
 * 这是逻辑模型，存档使用明确小端字段，不序列化编译器结构体布局。 */
typedef struct {
    uint64_t last_request_id;
    uint32_t exp;
    uint32_t coins;
    uint32_t saved_epoch_s;
    uint32_t daily_day;
    uint32_t inactive_s;
    uint32_t intimacy;
    int32_t mood;
    uint32_t energy;
    uint32_t hunger;
    uint32_t health;
    uint16_t feed_cooldown_s;
    uint16_t play_cooldown_s;
    uint16_t pet_count_today;
    uint32_t neglect_s;
    uint8_t level;
    uint8_t pet_cooldown_s;
    uint8_t last_action;
    bool sleeping;
    uint8_t task_progress[3];
    uint8_t task_claimed;
    uint8_t achievements;
    uint64_t last_dialogue_event,last_focus_event;
    uint8_t dialogues_today,focus_today;
} pet_growth_model_t;

void pet_growth_defaults(pet_growth_model_t *model);
bool pet_growth_model_valid(const pet_growth_model_t *model);
pet_growth_result_t pet_growth_apply(pet_growth_model_t *model, pet_growth_action_t action,
                                     uint64_t request_id);
void pet_growth_advance(pet_growth_model_t *model, uint32_t elapsed_s);
void pet_growth_advance_environment(pet_growth_model_t *model, uint32_t elapsed_s, uint32_t exposed_s);
void pet_growth_calendar(pet_growth_model_t *model, uint32_t epoch_s, int16_t utc_offset_min);
pet_growth_view_t pet_growth_view(const pet_growth_model_t *model, uint16_t local_minute,
                                  bool excited);

pet_growth_result_t pet_growth_award_completion(pet_growth_model_t *model,bool focus,uint64_t event_id);
