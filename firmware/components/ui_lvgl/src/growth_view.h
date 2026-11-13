#pragma once
#include "lvgl.h"
#include "pet/growth.h"
#include "growth_input.h"
#include "pet_avatar.h"
typedef struct {
    lv_obj_t *root, *title, *status, *stats, *action, *footer;
    const lv_font_t *small, *large;
    pet_avatar_t avatar;
    pet_growth_state_t state;
    bool animate;
} pet_growth_view_ui_t;
void pet_growth_view_ui_init(pet_growth_view_ui_t *view, lv_obj_t *root,
                            const lv_font_t *small, const lv_font_t *large);
void pet_growth_view_ui_show(pet_growth_view_ui_t *view, const pet_growth_view_t *snapshot,
                            const pet_growth_input_t *input, uint8_t unread, bool dnd,
                            bool pending, const char *feedback);
const char *pet_growth_feedback(pet_growth_result_t result);
/* visible=false（菜单/消息遮挡）停帧；返回 true 才需要刷新屏幕。 */
bool pet_growth_view_ui_tick(pet_growth_view_ui_t *view, uint32_t now_ms, bool visible);
