#include "growth_view.h"
#include <inttypes.h>
#include <string.h>
static lv_obj_t *label(lv_obj_t *root, int y, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(root);
    lv_obj_set_pos(obj, 14, y);
    lv_obj_set_width(obj, 208);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(obj, 6, 0);
    return obj;
}
void pet_growth_view_ui_init(pet_growth_view_ui_t *v, lv_obj_t *root,
                            const lv_font_t *small, const lv_font_t *large)
{
    memset(v, 0, sizeof(*v));
    v->root = root;
    v->small = small;
    v->large = large;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101923), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    v->title = label(root, 12, large, 0xF2F5F7);
    v->status = label(root, 48, small, 0x78C3A4);
    v->stats = label(root, 82, small, 0xDDE6ED);
    v->action = label(root, 216, small, 0xE5B461);
    v->footer = label(root, 270, small, 0xA9BDCC);
    pet_avatar_init(&v->avatar, root);
    pet_avatar_set_appearance(&v->avatar,&(pet_appearance_t){0},true);
    for(unsigned i=0;i<4;++i) {
        v->rows[i]=label(root,84+34*i,small,0xEDF4F2);
        lv_obj_set_height(v->rows[i],30);lv_obj_set_style_radius(v->rows[i],6,0);
        lv_obj_set_style_pad_left(v->rows[i],8,0);lv_obj_set_style_pad_top(v->rows[i],5,0);
    }
    for(unsigned i=0;i<3;++i) {
        v->tiles[i]=label(root,105,large,0xEDF4F2);lv_obj_set_pos(v->tiles[i],14+72*i,112);
        lv_obj_set_size(v->tiles[i],64,68);lv_obj_set_style_text_align(v->tiles[i],LV_TEXT_ALIGN_CENTER,0);
        lv_obj_set_style_pad_top(v->tiles[i],20,0);lv_obj_set_style_radius(v->tiles[i],12,0);
    }
}
const char *pet_growth_feedback(pet_growth_result_t result)
{
    switch (result) {
    case PET_GROWTH_OK: return "已完成并保存";
    case PET_GROWTH_TASK_INCOMPLETE: return "任务尚未完成";
    case PET_GROWTH_ALREADY_CLAIMED: return "奖励已领取";
    case PET_GROWTH_STALE_REQUEST: return "状态已过期，请重新选择";
    case PET_GROWTH_DUPLICATE: return "这次互动已完成";
    case PET_GROWTH_COOLDOWN: return "还在冷却，请稍后再试";
    case PET_GROWTH_NO_COINS: return "金币不足";
    case PET_GROWTH_LOW_ENERGY: return "精力不足，先休息一会儿";
    case PET_GROWTH_ASLEEP: return "正在睡觉，请先唤醒";
    case PET_GROWTH_WRONG_STATE: return "当前状态无需此操作";
    case PET_GROWTH_TIME_UNAVAILABLE: return "等待时间同步";
    case PET_GROWTH_STORAGE_UNCERTAIN: return "存档结果未确认，请重启";
    case PET_GROWTH_STORAGE_ERROR: return "保存失败，互动未结算";
    case PET_GROWTH_CONFLICT: return "状态已更新，请重新选择";
    default: return "暂时无法互动，请稍后再试";
    }
}
void pet_growth_view_ui_show(pet_growth_view_ui_t *v, const pet_growth_view_t *s,
                            const pet_growth_input_t *input, uint8_t unread, bool dnd,
                            bool pending, const char *feedback)
{
    static const char *states[] = {"自在发呆", "心情不错", "有新收获！", "有点低落", "有点困了", "肚子饿了", "需要照料", "正在睡觉"};
    /* 菜单保留完整的操作空间；主页把数值排紧，为角色留出固定画布。 */
    const bool home = !input->menu;
    const pet_menu_page_t *menu_page = input->menu ? pet_menu_page(input->page) : NULL;
    const uint8_t choice = pet_growth_input_selected_action(input);
    const char *choice_label = menu_page && input->selection < menu_page->count ?
        menu_page->items[input->selection].label : "";
    const bool achievements = choice == PET_GROWTH_MENU_ACHIEVEMENTS;
    lv_obj_set_style_text_font(v->title, home ? v->small : v->large, 0);
    lv_obj_set_y(v->title, home ? 8 : 12);
    lv_obj_set_y(v->status, home ? 34 : 48);
    lv_obj_set_y(v->stats, home ? 223 : 82);
    lv_obj_set_style_text_line_space(v->stats, home || achievements ? 0 : 6, 0);
    lv_obj_set_y(v->action, home ? 253 : 226);
    lv_obj_set_style_text_line_space(v->action, home ? 0 : 6, 0);
    lv_obj_set_y(v->footer, home ? 295 : 272);
    v->state = s ? s->state : PET_GROWTH_IDLE;
    /* 对时只门控成长结算；离线启动也应能看到宠物。 */
    v->animate = home && s && s->storage_ready;
    if (!v->animate) pet_avatar_tick(&v->avatar, v->state, lv_tick_get(), false);
    pet_avatar_set_environment(&v->avatar, s ? (pet_growth_environment_t)s->environment : PET_GROWTH_ENV_NONE);
    lv_label_set_text(v->title, home ? (*v->clock ? v->clock : "栖团 · 小舱") : achievements ? "成长成就" :
                      menu_page ? menu_page->title : "主菜单");
    if (!s) {
        lv_label_set_text(v->status, "成长服务暂不可用");
        lv_label_set_text(v->stats, "");
    } else {
        lv_label_set_text(v->status, !s->storage_ready ? "存档待恢复，请重启" :
                          !s->time_ready ? "等待时间同步" :
                          s->save_recovered ? "存档已回退，请核对数值" : states[(unsigned)s->state < 8 ? s->state : 0]);
        if (home && s->time_ready && s->storage_ready && !s->save_recovered &&
            s->environment > PET_GROWTH_ENV_NONE && s->environment <= PET_GROWTH_ENV_HUMID) {
            static const char *environment[] = {"", "有点冷", "有点热", "有点干", "有点湿"};
            lv_label_set_text_fmt(v->status, "%s · %s", states[(unsigned)s->state < 8 ? s->state : 0],
                                  environment[s->environment]);
        }
        if(home) lv_label_set_text_fmt(v->stats,"等级 %u   金币 %" PRIu32 "%s",s->level,s->coins,s->unsaved?" · 待存":"");
        else lv_label_set_text(v->stats,"");
    }
    if (pending) lv_label_set_text(v->action, "正在保存互动…");
    else if (input->menu) {
        unsigned cooldown = !s ? 0 : choice == PET_GROWTH_FEED ? s->feed_cooldown_s :
            choice == PET_GROWTH_PET ? s->pet_cooldown_s :
            choice == PET_GROWTH_PLAY ? s->play_cooldown_s : 0;
        if (s && achievements) {
            char friendship[32], level[32];
            lv_snprintf(friendship, sizeof(friendship), "亲密 %u/%u", s->intimacy, PET_GROWTH_FRIEND_TARGET);
            lv_snprintf(level, sizeof(level), "等级 %u/%u", s->level, PET_GROWTH_LEVEL_TARGET);
            if (s->time_ready && s->storage_ready && !s->save_recovered)
                lv_label_set_text_fmt(v->status, "%u/3 项已达成",
                    !!(s->achievements & PET_GROWTH_ACHIEVEMENT_CARE) +
                    !!(s->achievements & PET_GROWTH_ACHIEVEMENT_FRIEND) +
                    !!(s->achievements & PET_GROWTH_ACHIEVEMENT_LEVEL));
            lv_label_set_text_fmt(v->stats,
                "首次照料 · %u 金币\n%s\n亲密伙伴 · %u 金币\n%s\n成长起步 · %u 金币\n%s",
                PET_GROWTH_CARE_COINS, s->achievements & PET_GROWTH_ACHIEVEMENT_CARE ? "已达成" : "喂食/摸头/玩耍一次",
                PET_GROWTH_FRIEND_COINS, s->achievements & PET_GROWTH_ACHIEVEMENT_FRIEND ? "已达成" : friendship,
                PET_GROWTH_LEVEL_COINS, s->achievements & PET_GROWTH_ACHIEVEMENT_LEVEL ? "已达成" : level);
            lv_label_set_text(v->action, "成功操作时自动奖励");
        } else if (s && choice >= PET_GROWTH_CLAIM_FEED && choice <= PET_GROWTH_CLAIM_PLAY) {
            unsigned task = choice - PET_GROWTH_CLAIM_FEED;
            bool claimed = (s->task_claimed & (1u << task)) != 0;
            lv_label_set_text_fmt(v->action, "%u/%u · %s", s->task_progress[task],task==1?3:1,
                !s->task_day ? "等待对时" : claimed ? "已领取" :
                s->task_progress[task] >= (task == 1 ? 3 : 1) ? "可领取" : "未完成");
        } else if (cooldown) lv_label_set_text_fmt(v->action, "冷却 %u 秒", cooldown);
        else lv_label_set_text(v->action, "");
    } else if (feedback && *feedback) lv_label_set_text(v->action, feedback);
    else lv_label_set_text_fmt(v->action, "%u 条未读%s", unread, dnd ? " · 免打扰" : "");
    for(unsigned i=0;i<4;++i)lv_obj_add_flag(v->rows[i],LV_OBJ_FLAG_HIDDEN);
    for(unsigned i=0;i<3;++i)lv_obj_add_flag(v->tiles[i],LV_OBJ_FLAG_HIDDEN);
    if(input->menu && menu_page && !achievements) {
        if(input->page==PET_MENU_ROOT) {
            static const char *icons[]={"互","游","舱","成","伴","设"};
            for(unsigned i=0;i<3;++i) {
                unsigned n=(input->selection+menu_page->count+i-1)%menu_page->count;
                lv_label_set_text(v->tiles[i],icons[n%6]);lv_obj_remove_flag(v->tiles[i],LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_opa(v->tiles[i],LV_OPA_COVER,0);
                lv_obj_set_style_bg_color(v->tiles[i],lv_color_hex(i==1?0x91D4B7:0x20343E),0);
                lv_obj_set_style_text_color(v->tiles[i],lv_color_hex(i==1?0x172D32:0xA9BDCC),0);
            }
            lv_label_set_text_fmt(v->stats,"%u / %u",input->selection+1,menu_page->count);
            lv_label_set_text(v->action,choice_label);
        } else {
            unsigned first=input->selection<3?0:input->selection-2;
            if(first+4>menu_page->count)first=menu_page->count>4?menu_page->count-4:0;
            for(unsigned i=0;i<4 && first+i<menu_page->count;++i) {
                unsigned n=first+i;const pet_menu_item_t *item=&menu_page->items[n];
                lv_label_set_text_fmt(v->rows[i],"%s%s",item->label,
                    pet_growth_input_item_available(input,item)?"":" · 未启用");
                lv_obj_remove_flag(v->rows[i],LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_bg_opa(v->rows[i],n==input->selection?LV_OPA_COVER:LV_OPA_TRANSP,0);
                lv_obj_set_style_bg_color(v->rows[i],lv_color_hex(0x203D40),0);
                lv_obj_set_style_text_color(v->rows[i],lv_color_hex(n==input->selection?0x91D4B7:0xEDF4F2),0);
            }
            if(!pending && (!feedback||!*feedback)) {
                const pet_config_t *c=&v->settings;
                if(choice==PET_MENU_VOLUME_UP||choice==PET_MENU_VOLUME_DOWN)lv_label_set_text_fmt(v->action,"当前音量 %u",c->volume);
                if(choice==PET_MENU_BRIGHTNESS_UP||choice==PET_MENU_BRIGHTNESS_DOWN)lv_label_set_text_fmt(v->action,"当前亮度 %u",c->brightness);
                if(choice==PET_MENU_SFX_UP||choice==PET_MENU_SFX_DOWN)lv_label_set_text_fmt(v->action,"当前提示音 %u",c->sfx_volume);
                if(choice==PET_MENU_IDLE_BRIGHTNESS)lv_label_set_text_fmt(v->action,"待机亮度 %u",c->screen.idle_brightness);
                if(choice==PET_MENU_SCREEN_TIMEOUT)lv_label_set_text_fmt(v->action,"息屏 %u 秒 · 0 为关闭",c->screen.timeout_s);
                if(choice==PET_GROWTH_MENU_AI_MUTE)lv_label_set_text(v->action,c->muted?"已静音":"麦克风已开启");
                if(choice==PET_GROWTH_MENU_AI_DND)lv_label_set_text(v->action,dnd?"免打扰已开启":"免打扰已关闭");
            }
        }
    }
    if(pending)lv_label_set_text(v->action,"正在保存互动…");
    else if(feedback && *feedback)lv_label_set_text(v->action,feedback);
    lv_label_set_text(v->footer,input->menu?
        (input->navigation_enabled?"上下选择 · 右确认\n左返回 · C 语音":"A 确认  B 下一项\n长按 B 返回 · C 语音"):
        "A 菜单  B 消息  C 语音");
}

bool pet_growth_view_ui_tick(pet_growth_view_ui_t *view, uint32_t now, bool visible)
{
    return pet_avatar_tick(&view->avatar, view->state, now,
                           visible && view->animate && !lv_obj_has_flag(view->root, LV_OBJ_FLAG_HIDDEN));
}
