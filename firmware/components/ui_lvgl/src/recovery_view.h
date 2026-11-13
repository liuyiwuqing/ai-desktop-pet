#pragma once
#include "lvgl.h"
#include "pet/presentation/recovery_presenter.h"
typedef struct {lv_obj_t *root,*title,*status,*detail,*action;} pet_recovery_view_ui_t;
bool pet_recovery_view_ui_init(pet_recovery_view_ui_t *,lv_obj_t *,const lv_font_t *,const lv_font_t *);
void pet_recovery_view_ui_show(pet_recovery_view_ui_t *,pet_recovery_prompt_t,bool button_available);
