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
}
const char *pet_growth_feedback(pet_growth_result_t result)
{
    switch (result) {
    case PET_GROWTH_OK: return "已完成并保存";
    case PET_GROWTH_TASK_INCOMPLETE: return "任务尚未完成";
    case PET_GROWTH_ALREADY_CLAIMED: return "今日奖励已领取";
    case PET_GROWTH_STALE_REQUEST: return "任务已换日，请重新选择";
    case PET_GROWTH_DUPLICATE: return "这次互动已完成";
    case PET_GROWTH_COOLDOWN: return "还在冷却，请稍后再试";
    case PET_GROWTH_NO_COINS: return "金币不足，需要 5 金币";
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
    static const char *actions[] = {"喂食 · 5 金币", "摸头", "玩耍 · 10 精力", "睡觉", "唤醒", "喂食任务", "摸头任务", "玩耍任务", "成长成就", "温湿度", "开始 25 分钟专注", "暂停专注", "继续专注", "取消专注", "返回主页"};
    /* 菜单保留完整的操作空间；主页把数值排紧，为角色留出固定画布。 */
    const bool home = !input->menu;
    const bool achievements = input->menu && input->selection == PET_GROWTH_MENU_ACHIEVEMENTS;
    lv_obj_set_style_text_font(v->title, home ? v->small : v->large, 0);
    lv_obj_set_y(v->title, home ? 8 : 12);
    lv_obj_set_y(v->status, home ? 34 : 48);
    lv_obj_set_y(v->stats, home ? 142 : 82);
    lv_obj_set_style_text_line_space(v->stats, home || achievements ? 0 : 6, 0);
    lv_obj_set_y(v->action, home ? 248 : 216);
    lv_obj_set_style_text_line_space(v->action, home ? 0 : 6, 0);
    lv_obj_set_y(v->footer, home ? 294 : 270);
    v->state = s ? s->state : PET_GROWTH_IDLE;
    v->animate = home && s && s->time_ready && s->storage_ready;
    if (!v->animate) pet_avatar_tick(&v->avatar, v->state, lv_tick_get(), false);
    pet_avatar_set_environment(&v->avatar, s ? (pet_growth_environment_t)s->environment : PET_GROWTH_ENV_NONE);
    lv_label_set_text(v->title, "NEXUS PET");
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
        lv_label_set_text_fmt(v->stats,
            "等级 %u    金币 %" PRIu32 "\n经验 %" PRIu32 " / %" PRIu32
            "\n饱食 %u    精力 %u\n心情 %d    健康 %u\n亲密 %u%s",
            s->level, s->coins, s->exp, s->exp_to_next, s->hunger, s->energy,
            s->mood, s->health, s->intimacy, s->unsaved ? " · 待保存" : "");
    }
    if (pending) lv_label_set_text(v->action, "正在保存互动…");
    else if (input->menu) {
        unsigned choice = input->selection < PET_GROWTH_MENU_COUNT ? input->selection : PET_GROWTH_MENU_RETURN;
        unsigned cooldown = !s ? 0 : choice == 0 ? s->feed_cooldown_s : choice == 1 ? s->pet_cooldown_s : choice == 2 ? s->play_cooldown_s : 0;
        if (s && choice == PET_GROWTH_MENU_ACHIEVEMENTS) {
            char friendship[32], level[32];
            lv_snprintf(friendship, sizeof(friendship), "亲密 %u/%u", s->intimacy, PET_GROWTH_FRIEND_TARGET);
            lv_snprintf(level, sizeof(level), "等级 %u/%u", s->level, PET_GROWTH_LEVEL_TARGET);
            lv_label_set_text(v->title, "成长成就");
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
            lv_label_set_text_fmt(v->stats,
                "每日任务 · 04:00 重置\n喂食 %u/1   %s\n摸头 %u/3   %s\n玩耍 %u/1   %s\n每项 15 经验 + 5 金币",
                s->task_progress[0], s->task_claimed & 1 ? "已领" : "",
                s->task_progress[1], s->task_claimed & 2 ? "已领" : "",
                s->task_progress[2], s->task_claimed & 4 ? "已领" : "");
            lv_label_set_text_fmt(v->action, "%s · %s", actions[choice],
                !s->task_day ? "等待对时" : claimed ? "已领取" :
                s->task_progress[task] >= (task == 1 ? 3 : 1) ? "可领取" : "未完成");
        } else if (cooldown) lv_label_set_text_fmt(v->action, "%s\n冷却 %u 秒", actions[choice], cooldown);
        else lv_label_set_text(v->action, actions[choice]);
    } else if (feedback && *feedback) lv_label_set_text(v->action, feedback);
    else lv_label_set_text_fmt(v->action, "%u 条未读%s", unread, dnd ? " · 免打扰" : "");
    lv_label_set_text(v->footer, input->menu && input->selection == PET_GROWTH_MENU_ACHIEVEMENTS ?
                      "短按切换 · 长按返回" : input->menu ? "短按切换 · 长按执行" :
                      input->voice_enabled ? "短按消息 · 长按菜单" : "短按消息 · 长按互动");
}

bool pet_growth_view_ui_tick(pet_growth_view_ui_t *view, uint32_t now, bool visible)
{
    return pet_avatar_tick(&view->avatar, view->state, now,
                           visible && view->animate && !lv_obj_has_flag(view->root, LV_OBJ_FLAG_HIDDEN));
}
