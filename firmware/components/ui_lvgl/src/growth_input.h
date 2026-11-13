#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet/growth.h"
enum {
    PET_GROWTH_MENU_ACHIEVEMENTS = PET_GROWTH_ACTION_COUNT,
    PET_GROWTH_MENU_CLIMATE,
    PET_GROWTH_MENU_FOCUS,
    PET_GROWTH_MENU_FOCUS_PAUSE,
    PET_GROWTH_MENU_FOCUS_RESUME,
    PET_GROWTH_MENU_FOCUS_CANCEL,
    PET_GROWTH_MENU_RETURN,
    PET_GROWTH_MENU_COUNT,
    PET_GROWTH_MENU_AI_TALK = PET_GROWTH_MENU_COUNT,
    PET_GROWTH_MENU_AI_MUTE,
    PET_GROWTH_MENU_AI_DND,
    PET_GROWTH_MENU_AI_CLEAR,
    PET_GROWTH_INPUT_AI_RELEASE,
    PET_GROWTH_INPUT_AI_CANCEL,
};
/* 成就/返回不提交请求；AI 命令独立处理，不能转换为成长动作。 */
typedef struct {
    uint32_t changed_ms, pressed_ms;
    uint8_t selection;
    bool initialized, raw, stable, armed, menu;
    bool voice_enabled, voice_only, voice_active, voice_ready;
    bool holding, press_voice;
} pet_growth_input_t;
/* AI 项复用同一个按键代次；被消息/显示故障遮挡后，必须重新松开再按。 */
int pet_growth_input_update(pet_growth_input_t *input, bool pressed, bool blocked, uint32_t now);
