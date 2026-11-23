#include "ai_view.h"

#include <string.h>

static void set_text(lv_obj_t *label, const char *text)
{
    /* LVGL 相同文本也会重新分配并重绘；语音轮询只提交实际变化。 */
    if(strcmp(lv_label_get_text(label),text)) lv_label_set_text(label,text);
}

static lv_obj_t *label(lv_obj_t *root, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(root);
    lv_obj_set_pos(obj, 14, y);
    lv_obj_set_width(obj, 208);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(obj, 4, 0);
    return obj;
}

void pet_ai_view_ui_init(pet_ai_view_ui_t *v, lv_obj_t *root,
                          const lv_font_t *small, const lv_font_t *large)
{
    memset(v, 0, sizeof(*v));
    v->root = root;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101923), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    v->title = label(root, 12, large, 0xF2F5F7);
    v->status = label(root, 58, small, 0x78C3A4);
    v->detail = label(root, 236, small, 0xDDE6ED);
    lv_obj_set_height(v->detail,44);
    lv_label_set_long_mode(v->detail,LV_LABEL_LONG_DOT);
    v->footer = label(root, 294, small, 0xA9BDCC);
    pet_avatar_init(&v->avatar, root);
    lv_obj_set_y(v->avatar.object, 110);
    v->level = lv_bar_create(root);
    lv_obj_set_pos(v->level, 24, 214);
    lv_obj_set_size(v->level, 192, 8);
    lv_bar_set_range(v->level, 0, 100);
    lv_obj_set_style_bg_color(v->level, lv_color_hex(0x334456), LV_PART_MAIN);
    lv_obj_set_style_bg_color(v->level, lv_color_hex(0x78C3A4), LV_PART_INDICATOR);
}

void pet_ai_view_ui_show(pet_ai_view_ui_t *v, const pet_ai_screen_t *m,
                          const pet_growth_input_t *input, uint32_t now)
{
    const bool private = m->muted || m->dnd;
    const bool listening = m->available && m->ready && !m->faulted && !m->pending && !private &&
        m->capture_active && m->session.state == PET_AI_LISTENING;
    const bool speaking=m->available && m->ready && !m->faulted && !m->pending && !private && m->playback_active;
    const unsigned level = listening ? (m->session.level_pct>100 ? 100 : m->session.level_pct) :
        speaking ? (m->playback_level_pct>100 ? 100 : m->playback_level_pct) : 0;
    lv_bar_set_value(v->level, (int)level, LV_ANIM_OFF);
    if ((listening || speaking) && !input->menu) lv_obj_remove_flag(v->level, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(v->level, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_y(v->detail, input->menu ? 216 : 236);
    const char *title = "语音就绪", *status = "长按打开语音菜单";
    if (m->session.reason == PET_AI_REASON_NO_SPEECH) status = "没有听清，请重试";
    pet_growth_state_t face = PET_GROWTH_IDLE;
    if (m->faulted) {
        title = "音频异常";
        status = "音频状态未确认，请重启";
        face = PET_GROWTH_SICK;
    } else if (!m->available) {
        title = "语音未启用";
        status = "请先完成语音配置";
    } else if (!m->ready) {
        title = "语音启动中";
        status = "请稍候";
    } else if (m->pending) {
        title = "正在切换语音";
        status = "正在停止音频…";
    } else if (private || m->session.state == PET_AI_MUTED) {
        title = m->dnd ? "免打扰已开启" : "语音已静音";
        status = m->capture_active || m->playback_active ? "正在停止音频…" : "采集和播放已停止";
        face = PET_GROWTH_SLEEPING;
    } else if (m->session.state == PET_AI_LISTENING) {
        title = "正在听";
        status = m->capture_active ? "麦克风已开启" : "正在准备麦克风";
        face = PET_GROWTH_HAPPY;
    } else if (m->session.state == PET_AI_THINKING) {
        title = "正在思考";
        status = "正在等待回复";
    } else if (m->session.state == PET_AI_SPEAKING) {
        title = "正在说话";
        status = "正在播放回复";
        face = PET_GROWTH_HAPPY;
    } else if (m->session.state == PET_AI_ERROR) {
        title = "暂时无法对话";
        status = m->session.reason == PET_AI_REASON_RESPONSE_TIMEOUT ?
            (m->capture_active ? "回复超时，麦克风已开启" : "回复超时，请重试") :
            m->session.reason == PET_AI_REASON_TRANSPORT_ERROR ?
            (m->capture_active ? "连接失败，麦克风已开启" : "连接失败，请重试") :
            (m->capture_active ? "输入异常，麦克风已开启" : "音频输入异常，请重试");
        face = PET_GROWTH_SAD;
    } else if (m->session.state == PET_AI_IDLE && m->capture_active) {
        title = "等待唤醒";
        status = "请说“小爱同学”";
    }
    const lv_color_t status_color=lv_color_hex(m->faulted ||
        (!private && !m->pending && m->session.state == PET_AI_ERROR) ? 0xE5B461 : 0x78C3A4);
    if(!lv_color_eq(lv_obj_get_style_text_color(v->status,0),status_color))
        lv_obj_set_style_text_color(v->status,status_color,0);
    const char *detail="";
    char formatted[48];
    if (input->menu) {
        const uint8_t choice = pet_growth_input_selected_action(input);
        title="语音与隐私";
        if (m->available && m->ready && !m->faulted && !m->pending && !private)
            status=m->capture_active ?
                              "麦克风已开启" : choice == PET_GROWTH_MENU_AI_TALK ?
                              "看到“正在听”再说话" : "语音就绪";
        detail=choice == PET_GROWTH_MENU_AI_TALK ?
                (private ? "先关闭静音和免打扰" : !m->ready || m->faulted ? "语音暂不可用" : "按住说话") :
            choice == PET_GROWTH_MENU_AI_MUTE ? (m->muted ? "解除静音" : "静音") :
            choice == PET_GROWTH_MENU_AI_CLEAR ? "清除本地对话" :
            choice == PET_GROWTH_MENU_AI_DND ? (m->dnd ? "关闭免打扰" : "开启免打扰") : "返回主页";
    } else if (m->feedback) detail=m->feedback;
    else if (!private && m->reply && m->reply[0] && !listening) detail=m->reply;
    else if (!private && m->transcript && m->transcript[0]) detail=m->transcript;
    else if (listening) {lv_snprintf(formatted,sizeof(formatted),"输入电平 %u%%",level);detail=formatted;}
    else if (m->unread) {lv_snprintf(formatted,sizeof(formatted),"%u 条未读",m->unread);detail=formatted;}
    set_text(v->title,title);set_text(v->status,status);set_text(v->detail,detail);
    set_text(v->footer, input->navigation_enabled && input->menu ?
        (pet_growth_input_selected_action(input) == PET_GROWTH_MENU_AI_TALK ?
            "上下选择 · 按住中键说话" : "上下选择 · 右确认 · 左返回") :
        input->menu ?
        (pet_growth_input_selected_action(input) == PET_GROWTH_MENU_AI_TALK ? "短按切换 · 按住说话" : "短按切换 · 长按执行") :
        m->faulted ? "短按返回 · 长按菜单" :
        listening && m->session.push_to_talk ? "松开发送" :
        listening ? "安静片刻后发送" :
        !private && !m->pending && (m->session.state == PET_AI_THINKING || m->session.state == PET_AI_SPEAKING ||
            m->session.state == PET_AI_ERROR) ? "短按取消 · 长按菜单" :
        m->dismissible ? "短按返回 · 长按菜单" : "短按消息 · 长按菜单");
    pet_avatar_set_speech(&v->avatar,speaking&&!input->menu,(uint8_t)level);
    lv_obj_set_y(v->avatar.object,110+(!private && m->session.state==PET_AI_THINKING ? (int)(now/300%2)*2 : 0));
    pet_avatar_tick(&v->avatar, face, now, !input->menu);
}
