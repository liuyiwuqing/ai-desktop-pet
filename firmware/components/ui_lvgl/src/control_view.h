#pragma once
#include "lvgl.h"
typedef struct {lv_obj_t *root,*title,*target,*action,*remaining,*footer;} pet_control_view_ui_t;
void pet_control_view_ui_init(pet_control_view_ui_t *,lv_obj_t *,const lv_font_t *,const lv_font_t *);
void pet_control_view_ui_show(pet_control_view_ui_t *,const char *device,const char *application,unsigned seconds);
