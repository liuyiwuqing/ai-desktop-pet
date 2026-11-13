#pragma once
#include "lvgl.h"
#include "pet/climate.h"
typedef struct {
    lv_obj_t *root,*title,*values,*status,*alert,*footer,*chart,*temperature_legend,*humidity_legend;
    lv_chart_series_t *temperature,*humidity;
} pet_climate_view_ui_t;
void pet_climate_view_ui_init(pet_climate_view_ui_t *view,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large);
void pet_climate_view_ui_show(pet_climate_view_ui_t *view,const pet_climate_snapshot_t *snapshot,
                              const pet_climate_chart_t *chart,bool waiting);
