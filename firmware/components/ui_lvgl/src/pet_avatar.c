#include "pet_avatar.h"

#include <string.h>

#define INK 0x172D32
#define MINT 0x91D4B7
#define CREAM 0xE4E9C9

typedef struct { lv_layer_t *layer; int x, y, scale; } drawing_t;
static int sx(drawing_t *d,int x) { return d->x+x*d->scale/256; }
static int sy(drawing_t *d,int y) { return d->y+y*d->scale/256; }

static void round_rect(drawing_t *d, int x, int y, int w, int h, int radius, uint32_t color)
{
    lv_draw_rect_dsc_t desc;
    lv_draw_rect_dsc_init(&desc);
    desc.bg_color = lv_color_hex(color);
    desc.bg_opa = LV_OPA_COVER;
    desc.radius = radius*d->scale/256;
    const lv_area_t area = {sx(d,x),sy(d,y),sx(d,x+w)-1,sy(d,y+h)-1};
    lv_draw_rect(d->layer, &desc, &area);
}

static void line(drawing_t *d, int x1, int y1, int x2, int y2, int width, uint32_t color)
{
    lv_draw_line_dsc_t desc;
    lv_draw_line_dsc_init(&desc);
    desc.p1 = (lv_point_precise_t){sx(d,x1),sy(d,y1)};
    desc.p2 = (lv_point_precise_t){sx(d,x2),sy(d,y2)};
    desc.width = width*d->scale/256; if(desc.width<1)desc.width=1;
    desc.color = lv_color_hex(color);
    desc.round_start = desc.round_end = true;
    lv_draw_line(d->layer, &desc);
}

static void triangle(drawing_t *d, int x1, int y1, int x2, int y2, int x3, int y3, uint32_t color)
{
    lv_draw_triangle_dsc_t desc;
    lv_draw_triangle_dsc_init(&desc);
    desc.p[0] = (lv_point_precise_t){sx(d,x1),sy(d,y1)};
    desc.p[1] = (lv_point_precise_t){sx(d,x2),sy(d,y2)};
    desc.p[2] = (lv_point_precise_t){sx(d,x3),sy(d,y3)};
    desc.color = lv_color_hex(color);
    lv_draw_triangle(d->layer, &desc);
}

static void sparkle(drawing_t *d, int x, int y, int size)
{
    line(d, x - size, y, x + size, y, 2, 0xF0D68C);
    line(d, x, y - size, x, y + size, 2, 0xF0D68C);
}

/* Furnishings share a 24 px silhouette; no bitmap decoding or per-frame allocations. */
static void furnishing(drawing_t *d, unsigned item, int x, int y)
{
    switch (item) {
    case 7: round_rect(d,x+3,y,18,18,9,0xE5B461); line(d,x+12,y+18,x+12,y+29,3,0x849BA7); break;
    case 8:
        round_rect(d,x+5,y+14,16,14,3,0xB5876E);
        line(d,x+13,y+16,x+13,y,2,MINT);
        triangle(d,x+13,y+9,x,y+1,x+13,y+1,MINT);
        triangle(d,x+13,y+14,x+25,y+5,x+13,y+5,MINT); break;
    case 9:
        round_rect(d,x,y,27,27,3,0x3A5362);round_rect(d,x+3,y+3,21,21,1,INK);
        sparkle(d,x+13,y+13,5);break;
    case 10:
        triangle(d,x+3,y+23,x+13,y,x+23,y+23,0xEDF4F2);
        round_rect(d,x+9,y+11,7,7,4,MINT);line(d,x+13,y+25,x+13,y+30,4,0xE5B461);break;
    case 11:
        round_rect(d,x+5,y,18,14,5,0xE5B461);line(d,x+14,y+14,x+14,y+25,4,0xE5B461);
        line(d,x+6,y+27,x+22,y+27,3,0xE5B461);break;
    case 12:
        round_rect(d,x+1,y+3,23,23,3,0x91B5B6);line(d,x+6,y+4,x+6,y+26,2,INK);
        line(d,x+10,y+10,x+20,y+10,2,INK);break;
    default:break;
    }
}
static void draw(lv_event_t *event)
{
    const pet_avatar_t *avatar=lv_event_get_user_data(event);
    const pet_motion_pose_t *p=&avatar->pose;
    lv_area_t area;lv_obj_get_coords(avatar->object,&area);
    drawing_t d={.layer=lv_event_get_layer(event),.x=area.x1,.y=area.y1,
        .scale=lv_obj_get_height(avatar->object)>=150?256:128};
    if(d.scale==128)d.x+=52;
    const unsigned phase=(avatar->now_ms-avatar->reaction_ms)%1200;
    pet_reaction_t action=avatar->reaction;
    unsigned rare=0;
    if(action==PET_REACT_NONE && p->state<=PET_GROWTH_HAPPY && !avatar->speaking) {
        uint32_t idle=avatar->now_ms-avatar->motion.started_ms;
        if(idle%30000>=27500) rare=1+(idle/30000)%6;
    }
    bool asleep=p->state==PET_GROWTH_SLEEPING || action==PET_REACT_SLEEP;
    bool happy=p->state==PET_GROWTH_HAPPY || p->state==PET_GROWTH_EXCITED || action==PET_REACT_PET || action==PET_REACT_CHEER;
    bool flat=p->state==PET_GROWTH_SAD || p->state==PET_GROWTH_SICK || action==PET_REACT_FAILURE;
    int lift=p->lift+(action==PET_REACT_CHEER?(int)(phase%400<200?phase%200:200-phase%200)/20:0);
    int cx=104+p->sway+(rare==4?(int)(phase/120)%5-2:0);
    int floor=128-lift;
    int height=(asleep?56:80)+p->breath;
    int top=floor-height;
    int eyes=top+34;
    if(avatar->cabin) {
        static const uint32_t walls[]={0x1C2C3B,0x1B3430,0x202B45};
        round_rect(&d,0,0,208,149,16,walls[avatar->appearance.theme%3]);
        round_rect(&d,16,12,176,68,12,0x101923);
        for(int i=0;i<9;++i)round_rect(&d,25+(i*47)%157,20+(i*29)%48,2,2,1,0x6A879B);
        if(avatar->appearance.theme==1) {
            line(&d,22,72,22,24,3,0x91B5B6);line(&d,186,72,186,24,3,0x91B5B6);
            round_rect(&d,31,27,25,25,13,0x8BAE91);
        } else if(avatar->appearance.theme==2) {
            line(&d,153,31,176,45,3,0xE5B461);round_rect(&d,160,24,15,15,8,0xE5B461);
        } else round_rect(&d,157,24,20,20,10,0xB1C3CD);
        line(&d,10,132,198,132,1,0x405563);
        furnishing(&d,avatar->appearance.slots[0],14,100);
        furnishing(&d,avatar->appearance.slots[1],169,99);
        furnishing(&d,avatar->appearance.slots[2],166,51);
    }
    round_rect(&d,cx-35,130,70,7,4,0x101923);
    line(&d,cx+35,floor-12,cx+57,floor-25+p->gesture,7,0xEDF4F2);
    round_rect(&d,cx+52,floor-35+p->gesture,9,13,5,MINT);
    round_rect(&d,cx-31,floor-3,23,8,4,0xCBD9D8);
    round_rect(&d,cx+8,floor-3,23,8,4,0xCBD9D8);
    triangle(&d,cx-39,top+20,cx-31,top-17,cx-10,top+3,0xEDF4F2);
    triangle(&d,cx+8,top+4,cx+31,top-11+(rare==2?p->gesture:0),cx+40,top+20,0xEDF4F2);
    line(&d,cx-30,top-9,cx-23,top+2,3,MINT);
    line(&d,cx+29,top-4,cx+25,top+3,3,0xE5B461);
    round_rect(&d,cx-43,top,86,height,33,0xEDF4F2);
    round_rect(&d,cx-35,top+15,70,asleep?30:44,18,INK);
    int gaze=p->gaze+(rare==1?3:0);
    for(int side=-1;side<=1;side+=2) {
        int x=cx+side*16+gaze;
        if(asleep || p->blink || p->state==PET_GROWTH_SLEEPY || action==PET_REACT_PET)line(&d,x-6,eyes,x+6,eyes,3,MINT);
        else if(happy) {line(&d,x-6,eyes+2,x,eyes-3,3,MINT);line(&d,x,eyes-3,x+6,eyes+2,3,MINT);}
        else round_rect(&d,x-4,eyes-(flat?1:5),8,flat?4:12,4,MINT);
    }
    if(avatar->speaking || action==PET_REACT_SPEAK || rare==3)
        round_rect(&d,cx-5,eyes+12,10,4+(avatar->speech_level/15)+(rare==3?5:0),5,MINT);
    else line(&d,cx-4,eyes+14,cx+4,eyes+14,2,MINT);
    round_rect(&d,cx-13,floor-15,26,5,3,asleep?0x90A5A8:MINT);
    int arm=(action==PET_REACT_PET || action==PET_REACT_WAVE || rare==6)?-15:p->gesture;
    line(&d,cx-39,top+55,cx-48,top+61+arm,8,0xEDF4F2);
    line(&d,cx+39,top+55,cx+47,top+61-arm,8,0xEDF4F2);
    switch(avatar->appearance.outfit) {
    case 1:line(&d,cx-29,top+60,cx+29,top+60,5,0xE5B461);line(&d,cx+18,top+60,cx+23,top+74,5,0xE5B461);break;
    case 2:line(&d,cx-32,top-21,cx+32,top-21,3,0xE5B461);sparkle(&d,cx+34,top-20,4);break;
    case 3:line(&d,cx-26,top+53,cx+23,floor-6,3,0xB5876E);round_rect(&d,cx+16,floor-24,18,19,4,0xB5876E);break;
    case 4:line(&d,cx-32,eyes-9,cx+32,eyes-9,2,0xE5B461);round_rect(&d,cx+24,eyes-8,8,17,3,0xE5B461);break;
    case 5:line(&d,cx-43,top+18,cx-43,top+43,7,MINT);line(&d,cx+43,top+18,cx+43,top+43,7,MINT);break;
    case 6:line(&d,cx,top-2,cx,top-18,2,MINT);sparkle(&d,cx,top-20,5);break;
    default:break;
    }
    if(action==PET_REACT_FEED || p->state==PET_GROWTH_HUNGRY) {
        round_rect(&d,48,126,27,6,3,0x90A5A8);round_rect(&d,55,118-(int)(phase%300)/100,12,9,4,0xE5B461);
    } else if(action==PET_REACT_PET || action==PET_REACT_CHEER || p->state==PET_GROWTH_EXCITED) {
        sparkle(&d,45,45+p->gesture,5);sparkle(&d,166,34-p->gesture,6);
    } else if(action==PET_REACT_LISTEN) {
        line(&d,48,52,48,68,3,MINT);line(&d,40,57,40,63,3,MINT);
    } else if(action==PET_REACT_THINK || rare==1) {
        for(int i=0;i<3;++i)round_rect(&d,151+i*9,26,4,4,2,(phase/200)%3==(unsigned)i?MINT:0x405563);
    } else if(action==PET_REACT_FOCUS || rare==5) {
        round_rect(&d,76,119,55,22,4,0x6A879B);line(&d,103,121,103,139,2,INK);
    } else if(action==PET_REACT_WAKE || action==PET_REACT_ATTEND) {
        sparkle(&d,154,34,4);
    }
    if(p->state==PET_GROWTH_SICK)round_rect(&d,cx+28,eyes+7,4,9,2,0x80B5D6);
    if(asleep) {line(&d,155,27,167,27,2,MINT);line(&d,167,27,155,39,2,MINT);line(&d,155,39,167,39,2,MINT);}
    if(avatar->environment!=PET_GROWTH_ENV_NONE) {
        uint32_t cue=avatar->environment==PET_GROWTH_ENV_HOT?0xE5B461:0x80B5D6;
        if(avatar->environment==PET_GROWTH_ENV_HUMID)line(&d,26,40,22,49,3,cue);
        else if(avatar->environment==PET_GROWTH_ENV_DRY)line(&d,23,40,34,40,2,cue);
        else if(avatar->environment==PET_GROWTH_ENV_HOT)round_rect(&d,23,34,12,12,6,cue);
        else sparkle(&d,29,40,5);
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
    lv_obj_set_size(avatar->object, 208, 150);
    lv_obj_remove_flag(avatar->object, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(avatar->object, draw, LV_EVENT_DRAW_MAIN, avatar);
}

bool pet_avatar_tick(pet_avatar_t *avatar, pet_growth_state_t state, uint32_t now, bool visible)
{
    if (!visible) lv_obj_add_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(avatar->object, LV_OBJ_FLAG_HIDDEN);
    avatar->now_ms=now;
    if(avatar->reaction!=PET_REACT_NONE && now-avatar->reaction_ms>=1800)avatar->reaction=PET_REACT_NONE;
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

void pet_avatar_set_appearance(pet_avatar_t *avatar,const pet_appearance_t *appearance,bool cabin)
{
    if(!avatar||!appearance)return;
    if(memcmp(&avatar->appearance,appearance,sizeof(*appearance)) || avatar->cabin!=cabin) {
        avatar->appearance=*appearance; avatar->cabin=cabin;lv_obj_invalidate(avatar->object);
    }
}
void pet_avatar_react(pet_avatar_t *avatar,pet_reaction_t reaction,uint32_t now)
{
    if(!avatar || (unsigned)reaction>PET_REACT_WAVE)return;
    avatar->reaction=reaction;avatar->reaction_ms=now;lv_obj_invalidate(avatar->object);
}
