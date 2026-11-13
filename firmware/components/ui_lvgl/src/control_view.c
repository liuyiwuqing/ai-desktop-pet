#include "control_view.h"
static lv_obj_t *label(lv_obj_t *root,int y,int height,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *o=lv_label_create(root);lv_obj_set_pos(o,14,y);lv_obj_set_size(o,208,height);
    lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(o,4,0);lv_label_set_long_mode(o,LV_LABEL_LONG_WRAP);return o;
}
void pet_control_view_ui_init(pet_control_view_ui_t *v,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large)
{
    *v=(pet_control_view_ui_t){.root=root};lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_set_pos(root,0,0);lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101923),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    v->title=label(root,16,64,large,0xF2F5F7);v->target=label(root,90,44,small,0xA9BDCC);
    v->action=label(root,142,78,large,0xE5B461);v->remaining=label(root,234,22,small,0xA9BDCC);
    v->footer=label(root,270,42,small,0xF2F5F7);
}
void pet_control_view_ui_show(pet_control_view_ui_t *v,const char *device,const char *application,unsigned seconds)
{
    lv_label_set_text(v->title,"打开电脑应用");lv_label_set_text_fmt(v->target,"目标电脑 · %s",device);
    lv_label_set_text(v->action,application);lv_label_set_text_fmt(v->remaining,"%u 秒后取消",seconds);
    lv_label_set_text(v->footer,"短按 BOOT · 取消\n长按 BOOT · 确认");
}
