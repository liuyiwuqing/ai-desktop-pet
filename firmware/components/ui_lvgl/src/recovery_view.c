#include "recovery_view.h"

static lv_obj_t *label(lv_obj_t *root,int y,const lv_font_t *font,uint32_t color,const char *text)
{
    lv_obj_t *object=lv_label_create(root);
    lv_obj_set_pos(object,16,y);lv_obj_set_width(object,208);
    lv_obj_set_style_text_font(object,font,0);lv_obj_set_style_text_color(object,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(object,6,0);lv_label_set_text(object,text);return object;
}
bool pet_recovery_view_ui_init(pet_recovery_view_ui_t *v,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large)
{
    if(!v||!root||!small||!large)return false;
    lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101923),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    *v=(pet_recovery_view_ui_t){.root=root};
    v->title=label(root,24,large,0xFAFCFF,"恢复模式");
    v->status=label(root,78,small,0xE5B461,"网络、语音及扩展已暂停");
    v->detail=label(root,134,small,0xA9BDCC,"可用 USB 导出诊断\n重试不会清空存档");
    lv_obj_t *panel=lv_obj_create(root);lv_obj_remove_style_all(panel);
    lv_obj_set_pos(panel,12,210);lv_obj_set_size(panel,216,94);
    lv_obj_set_style_bg_color(panel,lv_color_hex(0x1B2B38),0);
    lv_obj_set_style_bg_opa(panel,LV_OPA_COVER,0);lv_obj_set_style_radius(panel,6,0);
    v->action=label(root,220,small,0xFAFCFF,"");
    return true;
}
void pet_recovery_view_ui_show(pet_recovery_view_ui_t *v,pet_recovery_prompt_t prompt,bool button_available)
{
    static const char *const text[]={
        "先松开 BOOT\n再准备重试启动", "长按 BOOT 3 秒\n准备重试启动",
        "松开 BOOT\n进入重试确认", "再次长按 BOOT 3 秒\n10 秒超时 · 短按取消",
        "松开 BOOT 后重启\n不会清空存档", "正在重新启动"
    };
    const unsigned index=(unsigned)prompt;
    lv_label_set_text(v->action,!button_available?"BOOT 不可用\n请连接 USB 维护":
        text[index<sizeof(text)/sizeof(text[0])?index:0]);
    lv_obj_remove_flag(v->root,LV_OBJ_FLAG_HIDDEN);lv_obj_invalidate(v->root);
}
