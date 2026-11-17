#include "setup_view.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *label(lv_obj_t *root,int y,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *value=lv_label_create(root);lv_obj_set_pos(value,14,y);lv_obj_set_width(value,212);
    lv_obj_set_style_text_font(value,font,0);lv_obj_set_style_text_color(value,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(value,3,0);return value;
}

/* 屏幕只用于连接，过长的自定义口令截断显示；完整值仍以用户设置为准。 */
static void clip(char *out,size_t capacity,const char *in,size_t max)
{
    size_t length=strlen(in);
    if(length<=max) {snprintf(out,capacity,"%s",in);return;}
    size_t keep=max-3;
    while(keep&&((unsigned char)in[keep]&0xC0)==0x80) keep--;
    memcpy(out,in,keep);memcpy(out+keep,"...",4);
}

void pet_setup_view_init(pet_setup_view_t *v,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large)
{
    *v=(pet_setup_view_t){.root=root};
    lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101923),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    v->title=label(root,12,large,0xF2F5F7);lv_label_set_text(v->title,"网络设置");
    v->state=label(root,50,small,0x78C3A4);lv_obj_set_height(v->state,42);lv_label_set_long_mode(v->state,LV_LABEL_LONG_DOT);
    /* 固定两行高度并主动截断：长名称/长口令不会压到下一块信息。 */
    v->ssid=label(root,100,small,0xDDE6ED);lv_obj_set_height(v->ssid,40);lv_label_set_long_mode(v->ssid,LV_LABEL_LONG_WRAP);
    v->password=label(root,146,small,0xDDE6ED);lv_obj_set_height(v->password,40);lv_label_set_long_mode(v->password,LV_LABEL_LONG_WRAP);
    v->address=label(root,193,small,0xDDE6ED);
    v->action=label(root,260,small,0xA9BDCC);
}

void pet_setup_view_show(pet_setup_view_t *v,const pet_setup_snapshot_t *s)
{
    static const char *const states[]={"手机连接下方 Wi-Fi","手机连接下方 Wi-Fi","正在连接家庭 Wi-Fi…",
        "Wi-Fi 已保存，可继续绑定语音","正在对时并连接小智…","请在 xiaozhi.me 添加设备",
        "语音配置已保存","管理台配置已保存","热点设置已保存","上一步未完成，请在网页重试","正在返回宠物主页…"};
    unsigned i=(unsigned)s->state;
    lv_label_set_text(v->state,states[i<sizeof(states)/sizeof(states[0])?i:9]);
    char ssid[28],password[48];
    clip(ssid,sizeof(ssid),s->ap_ssid,20);
    lv_label_set_text_fmt(v->ssid,"设备 Wi-Fi%s\n%s",s->ap_hidden?"（隐藏）":"",ssid);
    if(!s->ap_password[0]) lv_label_set_text(v->password,"连接密码\n开放网络，无需密码");
    else {
        clip(password,sizeof(password),s->ap_password,24);
        lv_label_set_text_fmt(v->password,"连接密码\n%s",password);
    }
    lv_label_set_text_fmt(v->address,"浏览器打开\nhttp://%s",PET_SETUP_HOST);
    if(s->activation_code[0]) lv_label_set_text_fmt(v->action,"小智绑定码 %s\n长按 BOOT 2 秒退出",s->activation_code);
    else lv_label_set_text_fmt(v->action,"长按 BOOT 2 秒退出\n约 %u 分钟后自动关闭",(unsigned)((s->remaining_s+59)/60));
}
