#pragma once

#include "lvgl.h"
#include "pet/message.h"

typedef struct {
    lv_obj_t *root;
    lv_obj_t *kind;
    lv_obj_t *time;
    lv_obj_t *mark;
    lv_obj_t *title;
    lv_obj_t *source;
    lv_obj_t *body_box;
    lv_obj_t *body;
    lv_obj_t *footer;
    lv_font_t *font16;
    lv_font_t *font24;
    uint32_t shown_ms;
    uint32_t last_scroll_ms;
    uint16_t missing_glyphs;
    int8_t shake_x;
    bool message_visible;
    bool alert_animation;
    bool read_failed;
    char text[PET_MESSAGE_BODY_MAX * 3 + 1];
} pet_message_view_ui_t;

bool pet_message_view_ui_init(pet_message_view_ui_t *view, lv_obj_t *root, lv_font_t *font16,
                              lv_font_t *font24);
void pet_message_view_ui_show(pet_message_view_ui_t *view, const pet_message_view_t *message,
                              bool plain_text, uint32_t now_ms, int16_t utc_offset_min,
                              bool alert_animation);
void pet_message_view_ui_idle(pet_message_view_ui_t *view, uint8_t unread, bool dnd);
void pet_message_view_ui_tick(pet_message_view_ui_t *view, uint32_t now_ms);
void pet_message_view_ui_scroll(pet_message_view_ui_t *view, uint32_t now_ms);
void pet_message_view_ui_read_failed(pet_message_view_ui_t *view);
