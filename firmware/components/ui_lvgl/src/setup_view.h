#pragma once
#include "lvgl.h"
#include "svc_setup/api.h"

typedef struct {lv_obj_t *root,*title,*state,*ssid,*password,*address,*action;} pet_setup_view_t;
void pet_setup_view_init(pet_setup_view_t *,lv_obj_t *,const lv_font_t *,const lv_font_t *);
void pet_setup_view_show(pet_setup_view_t *,const pet_setup_snapshot_t *);
