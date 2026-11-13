#pragma once

#include "growth_input.h"
#include "pet/ai.h"
#include "pet_avatar.h"

typedef struct {
    pet_ai_view_t session;
    bool available, ready, faulted, pending, muted, dnd;
    bool capture_active, playback_active;
    bool dismissible;
    uint8_t unread, playback_level_pct;
    const char *transcript,*reply;
    const char *feedback;
} pet_ai_screen_t;

typedef struct {
    lv_obj_t *root, *title, *status, *detail, *footer, *level;
    pet_avatar_t avatar;
} pet_ai_view_ui_t;

void pet_ai_view_ui_init(pet_ai_view_ui_t *view, lv_obj_t *root,
                          const lv_font_t *small, const lv_font_t *large);
/* 仅在 LVGL 拥有者调用；电平来自采集快照，思考/播放/静音不会伪造输入电平。 */
void pet_ai_view_ui_show(pet_ai_view_ui_t *view, const pet_ai_screen_t *model,
                          const pet_growth_input_t *input, uint32_t now_ms);
