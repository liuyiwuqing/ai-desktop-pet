#include "message_view.h"

#include <stdio.h>
#include <string.h>

/* 网络文本不启用富文本；非法 UTF-8/控制字符和字体缺字显示为方框。 */
static void safe_text(pet_message_view_ui_t *v, lv_obj_t *label, const char *text, size_t limit,
                      const lv_font_t *font)
{
    size_t used = 0;
    const unsigned char *p = (const unsigned char *)text;
    for (size_t at = 0; at < limit && p[at] != 0;) {
        const unsigned char first = p[at];
        size_t n = first < 0x80                     ? 1
                   : first >= 0xC2 && first <= 0xDF ? 2
                   : first >= 0xE0 && first <= 0xEF ? 3
                   : first >= 0xF0 && first <= 0xF4 ? 4
                                                    : 0;
        uint32_t code = n == 1   ? first
                        : n == 2 ? first & 0x1F
                        : n == 3 ? first & 0x0F
                                 : first & 0x07;
        bool valid = n != 0 && at + n <= limit;
        for (size_t j = 1; valid && j < n; ++j) {
            valid = (p[at + j] & 0xC0) == 0x80;
            code = (code << 6) | (p[at + j] & 0x3F);
        }
        valid = valid && !(n == 2 && code < 0x80) && !(n == 3 && code < 0x800) &&
                !(n == 4 && code < 0x10000) && code <= 0x10FFFF &&
                !(code >= 0xD800 && code <= 0xDFFF);
        lv_font_glyph_dsc_t glyph;
        const bool supported =
            valid && (code == '\n' || code == '\r' ||
                      (code >= 32 && code != 127 && lv_font_get_glyph_dsc(font, &glyph, code, 0) &&
                       !glyph.is_placeholder));
        if (supported) {
            if (used + n >= sizeof(v->text))
                break;
            memcpy(v->text + used, p + at, n);
            used += n;
        } else {
            if (used + 3 >= sizeof(v->text))
                break;
            memcpy(v->text + used, "□", 3);
            used += 3;
            ++v->missing_glyphs;
        }
        at += valid ? n : 1;
    }
    v->text[used] = '\0';
    lv_label_set_text(label, v->text);
}

static lv_obj_t *label(lv_obj_t *parent, int x, int y, int width, const lv_font_t *font,
                       uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_line_space(obj, 4, 0);
    return obj;
}

bool pet_message_view_ui_init(pet_message_view_ui_t *v, lv_obj_t *root, lv_font_t *font16,
                              lv_font_t *font24)
{
    memset(v, 0, sizeof(*v));
    v->root = root;
    v->font16 = font16;
    v->font24 = font24;
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, 240, 320);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101923), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 2, 0);
    lv_obj_set_style_border_color(root, lv_color_hex(0x334456), 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    v->mark = label(root, 14, 14, 22, LV_FONT_DEFAULT, 0xA9BDCC);
    v->kind = label(root, 40, 12, 112, font16, 0xA9BDCC);
    v->time = label(root, 152, 12, 74, font16, 0x9FB4C4);
    lv_obj_set_style_text_align(v->time, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_add_flag(v->time, LV_OBJ_FLAG_HIDDEN);
    v->title = label(root, 14, 40, 208, font24, 0xF2F5F7);
    lv_obj_set_height(v->title, 64);
    lv_label_set_long_mode(v->title, LV_LABEL_LONG_DOT);
    v->source = label(root, 14, 108, 208, font16, 0x9FB4C4);
    lv_label_set_long_mode(v->source, LV_LABEL_LONG_DOT);
    v->body_box = lv_obj_create(root);
    lv_obj_remove_style_all(v->body_box);
    lv_obj_set_pos(v->body_box, 14, 140);
    lv_obj_set_size(v->body_box, 208, 120);
    lv_obj_set_scroll_dir(v->body_box, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(v->body_box, LV_SCROLLBAR_MODE_OFF);
    v->body = label(v->body_box, 0, 0, 208, font16, 0xDDE6ED);
    lv_label_set_long_mode(v->body, LV_LABEL_LONG_WRAP);
    v->footer = label(root, 14, 294, 208, font16, 0xBBD1DF);
    return v->mark != NULL && v->kind != NULL && v->time != NULL && v->title != NULL &&
           v->source != NULL && v->body_box != NULL && v->body != NULL && v->footer != NULL;
}

/* 服务端 sentAt 按本机配置时区显示 HH:MM；未对时或时间戳缺失时隐藏，不显示伪造时间。 */
static void show_time(pet_message_view_ui_t *v, uint32_t epoch_s, int16_t utc_offset_min)
{
    const int64_t local = (int64_t)epoch_s + (int64_t)utc_offset_min * 60;
    if (epoch_s == 0 || local < 0 || utc_offset_min < -720 || utc_offset_min > 840) {
        lv_label_set_text(v->time, "");
        lv_obj_add_flag(v->time, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char text[8];
    snprintf(text, sizeof(text), "%02u:%02u", (unsigned)((local / 3600) % 24),
             (unsigned)((local / 60) % 60));
    lv_label_set_text(v->time, text);
    lv_obj_remove_flag(v->time, LV_OBJ_FLAG_HIDDEN);
}

static void reset_shake(pet_message_view_ui_t *v)
{
    v->alert_animation = false;
    if (v->shake_x != 0) {
        v->shake_x = 0;
        lv_obj_set_x(v->root, 0);
    }
}

void pet_message_view_ui_show(pet_message_view_ui_t *v, const pet_message_view_t *message,
                              bool plain_text, uint32_t now_ms, int16_t utc_offset_min,
                              bool alert_animation)
{
    static const char *const kinds[] = {"消息", "评论", "提醒", "成长", "系统"};
    const unsigned type = (unsigned)message->template_id;
    const bool critical = message->priority == PET_MESSAGE_PRIORITY_CRITICAL;
    const uint32_t border = critical                                                   ? 0xF07572
                            : message->template_id == PET_MESSAGE_TEMPLATE_ALERT_CARD  ? 0xE5B461
                            : message->template_id == PET_MESSAGE_TEMPLATE_GROWTH_CARD ? 0x78C3A4
                                                                                       : 0x5687A4;
    static const char *const marks[]={LV_SYMBOL_BELL,LV_SYMBOL_EDIT,LV_SYMBOL_WARNING,LV_SYMBOL_OK,LV_SYMBOL_SETTINGS};
    const bool comment=!plain_text&&message->template_id==PET_MESSAGE_TEMPLATE_COMMENT_CARD;
    const bool system=!plain_text&&message->template_id==PET_MESSAGE_TEMPLATE_SYSTEM_CARD;
    lv_label_set_text(v->mark,critical?LV_SYMBOL_WARNING:marks[type<PET_MESSAGE_TEMPLATE_COUNT?type:0]);
    lv_obj_set_style_text_color(v->mark,lv_color_hex(border),0);
    lv_obj_set_style_text_color(v->kind,lv_color_hex(critical?border:0xA9BDCC),0);
    /* 评论增加引用区，系统卡提高正文密度；长正文仍自动滚动且不隐式已读。 */
    lv_obj_set_pos(v->body_box,14,comment||system?130:140);
    lv_obj_set_size(v->body_box,208,comment||system?146:120);
    lv_obj_set_style_bg_color(v->body_box,lv_color_hex(comment?0x1B2B38:0x101923),0);
    lv_obj_set_style_bg_opa(v->body_box,comment?LV_OPA_COVER:LV_OPA_TRANSP,0);
    lv_obj_set_style_radius(v->body_box,6,0);
    lv_obj_set_pos(v->body,comment?10:0,comment?8:0);
    lv_obj_set_width(v->body,comment?188:208);
    lv_obj_set_style_border_color(v->root, lv_color_hex(plain_text ? 0x334456 : border), 0);
    lv_label_set_text(v->kind,
                      critical ? "紧急消息" : kinds[type < PET_MESSAGE_TEMPLATE_COUNT ? type : 0]);
    v->missing_glyphs = 0;
    lv_obj_set_style_text_font(v->title, plain_text ? v->font16 : v->font24, 0);
    safe_text(v, v->title, message->title, sizeof(message->title),
              plain_text ? v->font16 : v->font24);
    safe_text(v, v->source, message->source, sizeof(message->source), v->font16);
    safe_text(v, v->body, message->body, sizeof(message->body), v->font16);
    show_time(v, message->created_at_epoch_s, utc_offset_min);
    lv_obj_scroll_to_y(v->body_box, 0, LV_ANIM_OFF);
    lv_label_set_text(v->footer, "短按 BOOT · 标为已读");
    v->message_visible = true;
    v->read_failed = false;
    reset_shake(v);
    v->alert_animation = alert_animation && !plain_text;
    v->shown_ms = v->last_scroll_ms = now_ms;
    lv_obj_invalidate(v->root);
}

void pet_message_view_ui_idle(pet_message_view_ui_t *v, uint8_t unread, bool dnd)
{
    char title[48];
    snprintf(title, sizeof(title), unread ? "%u 条未读" : "暂无消息", unread);
    lv_obj_set_style_border_color(v->root, lv_color_hex(0x334456), 0);
    lv_label_set_text(v->mark,LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(v->mark,lv_color_hex(0xA9BDCC),0);
    lv_obj_set_style_text_color(v->kind,lv_color_hex(0xA9BDCC),0);
    lv_obj_set_style_bg_opa(v->body_box,LV_OPA_TRANSP,0);
    lv_obj_set_style_text_font(v->title, v->font24, 0);
    lv_label_set_text(v->kind, "NEXUS PET");
    lv_label_set_text(v->title, title);
    lv_label_set_text(v->source, dnd ? "免打扰已开启" : "收件箱");
    lv_label_set_text(v->body, "");
    lv_label_set_text(v->footer, unread ? "短按 BOOT · 查看消息" : "");
    lv_obj_scroll_to_y(v->body_box, 0, LV_ANIM_OFF);
    lv_label_set_text(v->time, "");
    lv_obj_add_flag(v->time, LV_OBJ_FLAG_HIDDEN);
    v->message_visible = false;
    v->read_failed = false;
    reset_shake(v);
    lv_obj_invalidate(v->root);
}

/* 紧急消息卡片在显示后 800 ms 内快速左右抖动，随后归位；手动浏览与纯文本回退不抖动。 */
void pet_message_view_ui_tick(pet_message_view_ui_t *v, uint32_t now_ms)
{
    if (!v->message_visible || !v->alert_animation)
        return;
    static const int8_t offsets[8] = {5, -5, 4, -4, 3, -3, 2, -2};
    const uint32_t elapsed = now_ms - v->shown_ms;
    int8_t x = 0;
    if (elapsed < 800U)
        x = offsets[elapsed / 100U];
    else
        v->alert_animation = false;
    if (x != v->shake_x) {
        v->shake_x = x;
        lv_obj_set_x(v->root, x);
    }
}

void pet_message_view_ui_scroll(pet_message_view_ui_t *v, uint32_t now_ms)
{
    if (!v->message_visible || now_ms - v->shown_ms < 1500 || now_ms - v->last_scroll_ms < 40)
        return;
    v->last_scroll_ms = now_ms;
    lv_obj_update_layout(v->body_box);
    const int total = lv_obj_get_y(v->body) + lv_obj_get_height(v->body) - lv_obj_get_height(v->body_box);
    if (total <= 0)
        return;
    int offset = lv_obj_get_scroll_y(v->body_box) + 1;
    if (offset > total)
        offset = total;
    lv_obj_scroll_to_y(v->body_box, offset, LV_ANIM_OFF);
    char footer[64];
    snprintf(footer, sizeof(footer), "%d%% · BOOT 标为已读", (int)(offset * 100 / total));
    if (!v->read_failed)
        lv_label_set_text(v->footer, footer);
}

void pet_message_view_ui_read_failed(pet_message_view_ui_t *v)
{
    v->read_failed = true;
    lv_label_set_text(v->footer, "确认失败 · 短按重试");
}
