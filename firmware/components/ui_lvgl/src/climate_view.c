#include "climate_view.h"
#include <string.h>
#include "pet/presentation/climate_card.h"
static lv_obj_t *label(lv_obj_t *root,int y,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *o=lv_label_create(root);lv_obj_set_pos(o,14,y);lv_obj_set_width(o,208);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);return o;
}
void pet_climate_view_ui_init(pet_climate_view_ui_t *v,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large)
{
    memset(v,0,sizeof(*v));v->root=root;lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(root,lv_color_hex(0x101923),0);
    lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    v->title=label(root,12,large,0xF2F5F7);lv_label_set_text(v->title,"室内温湿度");
    v->values=label(root,52,large,0x91D4B7);v->status=label(root,89,small,0xA9BDCC);
    v->chart=lv_chart_create(root);lv_obj_set_pos(v->chart,18,121);lv_obj_set_size(v->chart,204,99);
    lv_obj_set_style_bg_color(v->chart,lv_color_hex(0x142330),0);
    lv_obj_set_style_border_width(v->chart,0,0);lv_obj_set_style_size(v->chart,0,0,LV_PART_INDICATOR);
    lv_chart_set_point_count(v->chart,60);lv_chart_set_type(v->chart,LV_CHART_TYPE_LINE);
    lv_chart_set_axis_range(v->chart,LV_CHART_AXIS_PRIMARY_Y,-10,60);
    lv_chart_set_axis_range(v->chart,LV_CHART_AXIS_SECONDARY_Y,0,100);
    v->temperature=lv_chart_add_series(v->chart,lv_color_hex(0x91D4B7),LV_CHART_AXIS_PRIMARY_Y);
    v->humidity=lv_chart_add_series(v->chart,lv_color_hex(0x80B5D6),LV_CHART_AXIS_SECONDARY_Y);
    v->temperature_legend=label(root,224,small,0x91D4B7);lv_obj_set_width(v->temperature_legend,104);
    lv_label_set_text(v->temperature_legend,"温度 °C");
    v->humidity_legend=label(root,224,small,0x80B5D6);lv_obj_set_pos(v->humidity_legend,130,224);lv_obj_set_width(v->humidity_legend,96);
    lv_label_set_text(v->humidity_legend,"湿度 %");
    v->alert=label(root,247,small,0xE5B461);lv_obj_set_height(v->alert,40);
    v->footer=label(root,291,small,0xA9BDCC);
}
void pet_climate_view_ui_show(pet_climate_view_ui_t *v,const pet_climate_snapshot_t *s,const pet_climate_chart_t *chart,bool waiting)
{
    pet_climate_card_t card;pet_climate_card_render(s,&card);
    if (!s) {
        lv_label_set_text(v->values,"-- °C    -- %");lv_label_set_text(v->status,"传感器暂不可用");
    } else {
        lv_label_set_text_fmt(v->values,"%s   %s",card.temperature,card.humidity);
        lv_label_set_text_fmt(v->status,"%s · %s",card.comfort,card.updated);
    }
    for (unsigned i=0;i<60;++i) {
        const pet_climate_chart_point_t *p=chart?&chart->points[i]:NULL;
        lv_chart_set_value_by_id(v->chart,v->temperature,i,p&&p->valid?p->temperature_centi/100:LV_CHART_POINT_NONE);
        lv_chart_set_value_by_id(v->chart,v->humidity,i,p&&p->valid?p->humidity_centi/100:LV_CHART_POINT_NONE);
    }
    static const char *alerts[]={"最近 60 分钟","有点冷，注意保暖","有点热，注意通风","有点干，适当补水","有点湿，注意通风","注意凝露，远离电子设备"};
    lv_label_set_text(v->alert,!s||s->status==PET_CLIMATE_STATUS_FAULT?"采样失败，长按重试":
        waiting?"已请求重试，请稍候":alerts[(unsigned)s->alert<6?s->alert:0]);
    lv_label_set_text(v->footer,"短按返回 · 长按重试");
    lv_chart_refresh(v->chart);
}
