#include "pet_avatar.h"

#include <string.h>

#define INK 0x172D32
#define MINT 0x91D4B7
#define CREAM 0xE4E9C9

typedef struct { lv_layer_t *layer; int x, y; } drawing_t;

static void round_rect(drawing_t *d, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_draw_rect_dsc_t desc;
    lv_draw_rect_dsc_init(&desc);
    desc.bg_color = lv_color_hex(color);
    desc.bg_opa = LV_OPA_COVER;
    desc.radius = radius;
    const lv_area_t area = {d->x + x, d->y + y, d->x + x + w - 1, d->y + y + h - 1};
    lv_draw_rect(d->layer, &desc, &area);
}

static void line(drawing_t *d, int x1, int y1, int x2, int y2, int width, uint32_t color)
{
    lv_draw_line_dsc_t desc;
    lv_draw_line_dsc_init(&desc);
    desc.p1 = (lv_point_precise_t){d->x + x1, d->y + y1};
    desc.p2 = (lv_point_precise_t){d->x + x2, d->y + y2};
    desc.width = width;
    desc.color = lv_color_hex(color);
    desc.round_start = desc.round_end = true;
    lv_draw_line(d->layer, &desc);
}

static void triangle(drawing_t *d, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color)
{
    lv_draw_triangle_dsc_t desc;
    lv_draw_triangle_dsc_init(&desc);
    desc.p[0] = (lv_point_precise_t){d->x + x1, d->y + y1};
    desc.p[1] = (lv_point_precise_t){d->x + x2, d->y + y2};
    desc.p[2] = (lv_point_precise_t){d->x + x3, d->y + y3};
    desc.color = lv_color_hex(color);
    lv_draw_triangle(d->layer, &desc);
}

static void sparkle(drawing_t *d, int x, int y, int size)
{
    line(d, x - size, y, x + size, y, 2, 0xF0D68C);
    line(d, x, y - size, x, y + size, 2, 0xF0D68C);
}

static void draw(lv_event_t *event)
{
    const pet_avatar_t *avatar = lv_event_get_user_data(event);
    const pet_motion_pose_t *p = &avatar->pose;
    lv_area_t area;
    lv_obj_get_coords(avatar->object, &area);
    drawing_t d = {.layer = lv_event_get_layer(event), .x = area.x1, .y = area.y1};
    const bool asleep = p->state == PET_GROWTH_SLEEPING;
    const bool sick = p->state == PET_GROWTH_SICK;
    const bool sad = p->state == PET_GROWTH_SAD;
    const bool happy = p->state == PET_GROWTH_HAPPY || p->state == PET_GROWTH_EXCITED;
    /* 环境动作只叠加在普通发呆/开心上，不改睡眠、疾病、饥饿或庆祝的主表现。 */
    const bool ordinary = p->state == PET_GROWTH_IDLE || p->state == PET_GROWTH_HAPPY;
    const bool cold = ordinary && avatar->environment == PET_GROWTH_ENV_COLD;
    const bool hot = ordinary && avatar->environment == PET_GROWTH_ENV_HOT;
    const bool dry = ordinary && avatar->environment == PET_GROWTH_ENV_DRY;
    const bool humid = ordinary && avatar->environment == PET_GROWTH_ENV_HUMID;
    const int cx = 104 + p->sway + (cold ? p->gesture / 2 : 0);
    const int floor = 68 - p->lift;
    const int height = (asleep ? 32 : sick ? 40 : 48) + p->breath - (100 - p->entry_pct) / 25;
    const int top = floor - height;
    const int eyes = top + (asleep ? 17 : 23);
    const uint32_t body = sick ? 0xB7C5A2 : sad ? 0x91B5B6 : cold ? 0x91BFCE :
                          hot ? 0xBFCB9D : dry ? 0xB5C7AA : humid ? 0x7EBDB4 : MINT;
    round_rect(&d, 77 + p->lift / 2, 67, 54 - p->lift, 5, 4, 0x20343E);
    /* 尾巴、脚和耳朵在身体下层，保持一个连续的角色轮廓。 */
    line(&d, cx + 25, floor - 10, cx + 42, floor - 17 + p->gesture, 7, body);
    line(&d, cx + 42, floor - 17 + p->gesture, cx + 40, floor - 27 + p->gesture, 7, body);
    round_rect(&d, cx - 23, floor - 4, 17, 7, 4, body);
    round_rect(&d, cx + 6, floor - 4, 17, 7, 4, body);
    triangle(&d, cx - 28, top + 17, cx - 25, top - 8, cx - 6, top + 4, body);
    triangle(&d, cx + 6, top + 4, cx + 25, top - 8, cx + 28, top + 17, body);
    triangle(&d, cx - 23, top + 7, cx - 23, top - 1, cx - 14, top + 5, CREAM);
    triangle(&d, cx + 14, top + 5, cx + 23, top - 1, cx + 23, top + 7, CREAM);
    round_rect(&d, cx - 30, top, 60, height, 23, body);
    round_rect(&d, cx - 18, floor - 17, 36, 15, 8, CREAM);
    if (happy) {
        line(&d, cx - 30, top + 28, cx - 39, top + 21 - p->gesture, 6, body);
        line(&d, cx + 30, top + 28, cx + 38, top + 20 + p->gesture, 6, body);
    }
    for (int side = -1; side <= 1; side += 2) {
        const int x = cx + side * 12 + p->gaze;
        if (asleep || p->blink || p->state == PET_GROWTH_SLEEPY) {
            line(&d, x - 4, eyes, x + 4, eyes + (asleep ? 1 : 0), 2, INK);
        } else if (happy) {
            line(&d, x - 4, eyes + 2, x, eyes - 1, 2, INK);
            line(&d, x, eyes - 1, x + 4, eyes + 2, 2, INK);
        } else {
            round_rect(&d, x - 3, eyes - 4, 6, 9, 3, INK);
            round_rect(&d, x - 1, eyes - 3, 2, 2, 1, 0xF6F1DC);
        }
        if (sad || sick) line(&d, x - 5, eyes - 9 + side, x + 4, eyes - 9 - side, 2, INK);
        if (happy) round_rect(&d, x - 7, eyes + 8, 8, 3, 2, 0xD2A994);
    }
    if (avatar->speaking) {
        int h=2+(int)avatar->speech_level/12;
        round_rect(&d,cx-4,eyes+9,8,h,4,INK);
    } else if (p->state == PET_GROWTH_HUNGRY) {
        round_rect(&d, cx - 4, eyes + 10, 8, 8, 4, INK);
        round_rect(&d, 42, 58, 23, 5, 3, 0xBBC8D0);
        round_rect(&d, 48, 52 + p->gesture / 2, 10, 7, 3, 0xDEB878);
    } else if (sad || sick) {
        line(&d, cx - 5, eyes + 14, cx, eyes + 11, 2, INK);
        line(&d, cx, eyes + 11, cx + 5, eyes + 14, 2, INK);
    } else if (dry) {
        line(&d, cx - 5, eyes + 12, cx + 5, eyes + 12, 2, INK);
    } else {
        line(&d, cx - 5, eyes + 10, cx, eyes + 13, 2, INK);
        line(&d, cx, eyes + 13, cx + 5, eyes + 10, 2, INK);
    }
    if (sick) {
        triangle(&d, cx + 26, top + 2, cx + 22, top + 13, cx + 30, top + 13, 0x80B5D6);
        round_rect(&d, cx + 22, top + 10, 9, 8, 4, 0x80B5D6);
    } else if (sad) {
        round_rect(&d, cx - 22, eyes + 8 + p->gesture, 3, 6, 2, 0x80B5D6);
    } else if (asleep || p->state == PET_GROWTH_SLEEPY) {
        const int y = 15 + p->gesture;
        line(&d, 151, y, 161, y, 2, 0xA9BDCC);
        line(&d, 161, y, 151, y + 9, 2, 0xA9BDCC);
        line(&d, 151, y + 9, 161, y + 9, 2, 0xA9BDCC);
    } else if (p->state == PET_GROWTH_EXCITED) {
        sparkle(&d, 58, 23 + p->gesture, 4);
        sparkle(&d, 153, 28 - p->gesture, 5);
    }
    if (cold) {
        round_rect(&d, cx - 28, top + 33, 56, 5, 2, 0xA9BDCC);
        line(&d, cx - 23, top + 34, cx - 5, top + 41 + p->gesture / 2, 5, body);
        line(&d, cx + 23, top + 34, cx + 5, top + 41 - p->gesture / 2, 5, body);
    } else if (hot) {
        line(&d, cx + 24, top + 31, cx + 37, top + 29 + p->gesture, 5, body);
        line(&d, cx + 37, top + 29 + p->gesture, cx + 42, top + 20 + p->gesture, 2, 0xDEB878);
        triangle(&d, cx + 32, top + 11 + p->gesture, cx + 50, top + 14 + p->gesture,
                 cx + 42, top + 24 + p->gesture, 0xB8D9DB);
    } else if (dry) {
        line(&d, cx + 26, top + 33, cx + 9, eyes + 14 + p->gesture / 2, 5, body);
    }
    const int cue_y = 25 + p->gesture / 2;
    switch (avatar->environment) {
    case PET_GROWTH_ENV_COLD:
        line(&d, 34, cue_y, 50, cue_y, 2, 0x80B5D6);
        line(&d, 42, cue_y - 8, 42, cue_y + 8, 2, 0x80B5D6);
        line(&d, 36, cue_y - 6, 48, cue_y + 6, 2, 0x80B5D6);
        line(&d, 36, cue_y + 6, 48, cue_y - 6, 2, 0x80B5D6);
        break;
    case PET_GROWTH_ENV_HOT:
        round_rect(&d, 37, cue_y - 5, 11, 11, 6, 0xE5B461);
        line(&d, 32, cue_y, 52, cue_y, 2, 0xE5B461);
        line(&d, 42, cue_y - 10, 42, cue_y + 10, 2, 0xE5B461);
        break;
    case PET_GROWTH_ENV_DRY:
        line(&d, 32, cue_y - 6, 52, cue_y - 6, 2, 0xDEB878);
        line(&d, 32, cue_y + 8, 52, cue_y + 8, 2, 0xDEB878);
        line(&d, 43, cue_y - 4, 39, cue_y, 2, 0xDEB878);
        line(&d, 39, cue_y, 45, cue_y + 4, 2, 0xDEB878);
        break;
    case PET_GROWTH_ENV_HUMID:
        round_rect(&d, 31, cue_y - 6, 23, 10, 5, 0xA9BDCC);
        round_rect(&d, 36, cue_y - 10, 12, 12, 6, 0xA9BDCC);
        line(&d, 37, cue_y + 8, 35, cue_y + 12, 2, 0x80B5D6);
        line(&d, 48, cue_y + 8, 46, cue_y + 12, 2, 0x80B5D6);
        break;
    default: break;
    }
}

void pet_avatar_set_environment(pet_avatar_t *avatar, pet_growth_environment_t environment)
{
    if ((unsigned)environment > PET_GROWTH_ENV_HUMID) environment = PET_GROWTH_ENV_NONE;
    if (avatar->environment == environment) return;
    avatar->environment = environment;
    if (!lv_obj_has_flag(avatar->object, LV_OBJ_FLAG_HIDDEN)) lv_obj_invalidate(avatar->object);
}

void pet_avatar_init(pet_avatar_t *avatar, lv_obj_t *parent)
{
    memset(avatar, 0, sizeof(*avatar));
    avatar->object = lv_obj_create(parent);
    lv_obj_remove_style_all(avatar->object);
    lv_obj_set_pos(avatar->object, 14, 62);
    lv_obj_set_size(avatar->object, 208, 78);
    lv_obj_remove_flag(avatar->object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(avatar->object, draw, LV_EVENT_DRAW_MAIN, avatar);
}

bool pet_avatar_tick(pet_avatar_t *avatar, pet_growth_state_t state, uint32_t now, bool visible)
{
    if (!visible) lv_obj_add_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    bool changed = pet_motion_step(&avatar->motion, state, now, visible, &avatar->pose);
    if (changed) lv_obj_invalidate(avatar->object);
    return changed;
}

void pet_avatar_set_speech(pet_avatar_t *avatar,bool speaking,uint8_t level)
{
    if (!avatar) return;
    uint8_t value=speaking ? (level>100?100:level) : 0;
    if (avatar->speaking!=speaking || avatar->speech_level!=value) {
        avatar->speaking=speaking; avatar->speech_level=value; lv_obj_invalidate(avatar->object);
    }
}
