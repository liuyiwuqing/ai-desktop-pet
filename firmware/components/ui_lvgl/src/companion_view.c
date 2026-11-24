#include "companion_view.h"
#include "growth_view.h"
#include <inttypes.h>
#include <string.h>

static lv_obj_t *text(lv_obj_t *root,int y,const lv_font_t *font,uint32_t color)
{
    lv_obj_t *obj=lv_label_create(root);lv_obj_set_pos(obj,14,y);lv_obj_set_width(obj,212);
    lv_obj_set_style_text_font(obj,font,0);lv_obj_set_style_text_color(obj,lv_color_hex(color),0);
    lv_obj_set_style_text_line_space(obj,5,0);return obj;
}
static void rect(lv_layer_t *layer,int x,int y,int w,int h,uint32_t color,int radius)
{
    lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_color=lv_color_hex(color);d.bg_opa=LV_OPA_COVER;d.radius=radius;
    lv_area_t a={x,y,x+w-1,y+h-1};lv_draw_rect(layer,&d,&a);
}
static void symbol(lv_layer_t *layer,unsigned n,int x,int y,int size,uint32_t color)
{
    if(n==0)rect(layer,x,y,size,size,color,size/2);
    else if(n==1)rect(layer,x,y,size,size,color,2);
    else if(n==3) {
        lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.color=lv_color_hex(color);
        d.p[0]=(lv_point_precise_t){x+size/2,y};d.p[1]=(lv_point_precise_t){x,y+size/2};d.p[2]=(lv_point_precise_t){x+size,y+size/2};
        lv_draw_triangle(layer,&d);d.p[0].y=y+size;lv_draw_triangle(layer,&d);
    } else {
        lv_draw_triangle_dsc_t d;lv_draw_triangle_dsc_init(&d);d.color=lv_color_hex(color);
        d.p[0]=(lv_point_precise_t){x+size/2,y};d.p[1]=(lv_point_precise_t){x,y+size};d.p[2]=(lv_point_precise_t){x+size,y+size};
        lv_draw_triangle(layer,&d);
    }
}
static void game_draw(lv_event_t *event)
{
    const pet_companion_ui_t *ui=lv_event_get_user_data(event);
    lv_layer_t *layer=lv_event_get_layer(event);lv_area_t a;lv_obj_get_coords(ui->canvas,&a);
    const pet_game_t *g=&ui->game;
    if(g->kind==PET_GAME_STARS) {
        for(unsigned i=0;i<3;++i) {
            rect(layer,a.x1+8+65*i,a.y1+5,57,145,0x1C2C3B,8);
            if(i==g->selection)rect(layer,a.x1+23+65*i,a.y1+127,28,12,0x91D4B7,5);
        }
        int y=(int)((g->elapsed_ms-g->round_ms)*110/1100);
        symbol(layer,g->debris?1:2,a.x1+28+65*g->target,a.y1+12+y,16,g->debris?0xB5876E:0xE5B461);
        if(g->elapsed_ms<g->shield_until_ms)rect(layer,a.x1+17+65*g->selection,a.y1+119,40,4,0x80B5D6,2);
    } else if(g->kind==PET_GAME_SIGNAL) {
        unsigned n=(g->elapsed_ms-g->round_ms)/650;
        unsigned sequence_length=3+g->round;
        if(g->showing && n<sequence_length && (g->elapsed_ms-g->round_ms)%650<500)
            symbol(layer,g->sequence[n],a.x1+85,a.y1+18,38,0xE5B461);
        for(unsigned i=0;i<4;++i) {
            rect(layer,a.x1+5+52*i,a.y1+98,45,48,i==g->selection?0x31544D:0x1C2C3B,8);
            symbol(layer,i,a.x1+17+52*i,a.y1+110,21,0x91D4B7);
        }
        for(unsigned i=0;i<sequence_length;++i)rect(layer,a.x1+35+23*i,a.y1+76,12,4,i<g->matched?0x91D4B7:0x405563,2);
    } else {
        rect(layer,a.x1+10,a.y1+60,190,20,0x1C2C3B,10);
        rect(layer,a.x1+86,a.y1+60,38,20,0x31544D,3);
        rect(layer,a.x1+102,a.y1+48,3,44,0xE5B461,1);
        int x=10+(int)pet_game_position(g)*184/100;
        rect(layer,a.x1+x,a.y1+55,8,30,0xEDF4F2,4);
        for(unsigned i=0;i<5;++i)rect(layer,a.x1+53+22*i,a.y1+120,12,5,i<g->round?0x91D4B7:0x405563,2);
    }
}
void pet_companion_ui_init(pet_companion_ui_t *ui,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large)
{
    memset(ui,0,sizeof(*ui));ui->root=root;lv_obj_remove_style_all(root);lv_obj_set_size(root,240,320);
    lv_obj_set_style_bg_color(root,lv_color_hex(0x101923),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
    lv_obj_remove_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    ui->title=text(root,12,large,0xEDF4F2);ui->status=text(root,48,small,0x91D4B7);
    ui->detail=text(root,226,small,0xE5B461);ui->footer=text(root,274,small,0xA9BDCC);
    pet_avatar_init(&ui->avatar,root);
    ui->canvas=lv_obj_create(root);lv_obj_remove_style_all(ui->canvas);lv_obj_set_pos(ui->canvas,14,68);
    lv_obj_set_size(ui->canvas,212,152);lv_obj_remove_flag(ui->canvas,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ui->canvas,game_draw,LV_EVENT_DRAW_MAIN,ui);
    lv_obj_add_flag(root,LV_OBJ_FLAG_HIDDEN);
}
void pet_companion_ui_open(pet_companion_ui_t *ui,pet_space_page_t page,unsigned game,uint32_t now)
{
    ui->page=page;ui->selection=0;ui->slot=0;ui->choose_slot=page==PET_SPACE_FURNITURE;
    ui->pending=ui->game_saved=ui->game_attempted=false;ui->feedback=NULL;
    if(page==PET_SPACE_GAME)pet_game_init(&ui->game,(pet_game_kind_t)game,now|1,now);
}
static unsigned count(const pet_companion_ui_t *ui)
{
    switch(ui->page) {
    case PET_SPACE_WARDROBE:return 7;
    case PET_SPACE_FURNITURE:return ui->choose_slot?3:7;
    case PET_SPACE_THEMES:return 3;
    case PET_SPACE_MEMORIES:return ui->snapshot.state.memory_sequence<8?ui->snapshot.state.memory_sequence:8;
    default:return 1;
    }
}
bool pet_companion_ui_input(pet_companion_ui_t *ui,pet_space_input_t input,uint32_t now,pet_companion_request_t *r)
{
    if(!ui || !r || ui->page==PET_SPACE_CLOSED || ui->pending)return false;
    ui->feedback=NULL;memset(r,0,sizeof(*r));
    if(ui->page==PET_SPACE_GAME) {
        if(input==PET_SPACE_BACK) {
            if(ui->game.phase==PET_GAME_RUNNING)pet_game_input(&ui->game,PET_GAME_PAUSE,now);
            else ui->page=PET_SPACE_CLOSED;
        } else if(ui->game.phase==PET_GAME_RESULT) {
            if(input==PET_SPACE_CONFIRM) {
                if(ui->game_saved)pet_companion_ui_open(ui,PET_SPACE_GAME,ui->game.kind,now);
                else {
                    r->operation=PET_COMPANION_GAME;r->game=ui->game.kind;r->score=ui->game.score;
                    ui->game_attempted=ui->pending=true;return true;
                }
            }
        } else if(ui->game.phase==PET_GAME_PAUSED) {
            if(input==PET_SPACE_CONFIRM)pet_game_input(&ui->game,PET_GAME_RESUME,now);
        } else pet_game_input(&ui->game,input==PET_SPACE_CONFIRM?PET_GAME_CONFIRM:
            input==PET_SPACE_PREVIOUS?PET_GAME_PREVIOUS:PET_GAME_NEXT,now);
        return false;
    }
    if(input==PET_SPACE_BACK) {
        if(ui->page==PET_SPACE_FURNITURE && !ui->choose_slot) {ui->choose_slot=true;ui->selection=ui->slot;}
        else ui->page=PET_SPACE_CLOSED;
        return false;
    }
    if(input==PET_SPACE_NEXT||input==PET_SPACE_PREVIOUS) {
        unsigned n=count(ui);if(n)ui->selection=(ui->selection+n+(input==PET_SPACE_NEXT?1:n-1))%n;
        return false;
    }
    if(ui->page==PET_SPACE_ATTRIBUTES||ui->page==PET_SPACE_MEMORIES)return false;
    if(ui->page==PET_SPACE_FURNITURE && ui->choose_slot) {
        ui->slot=ui->selection;ui->selection=0;ui->choose_slot=false;return false;
    }
    if(!ui->snapshot.writable) {ui->feedback="暂不可保存，请稍后重试";return false;}
    r->expected_revision=ui->snapshot.state.appearance_revision;
    r->appearance=ui->snapshot.state.appearance;r->operation=PET_COMPANION_DRESS;
    if(ui->page==PET_SPACE_THEMES) {
        if(!(ui->snapshot.themes&(1u<<ui->selection))) {ui->feedback="完成解锁条件后再来";return false;}
        r->appearance.theme=ui->selection;
    } else {
        unsigned item=ui->selection+(ui->page==PET_SPACE_FURNITURE && ui->selection?6:0);
        if(item && !(ui->snapshot.state.owned&(1u<<(item-1)))) {
            if(!(ui->snapshot.eligible&(1u<<(item-1)))) {ui->feedback="还未达到解锁条件";return false;}
            r->operation=PET_COMPANION_BUY;r->item=item;
        } else if(ui->page==PET_SPACE_WARDROBE)r->appearance.outfit=item;
        else {
            for(unsigned i=0;i<3;++i)if(r->appearance.slots[i]==item)r->appearance.slots[i]=0;
            r->appearance.slots[ui->slot]=item;
        }
    }
    ui->pending=true;return true;
}
void pet_companion_ui_complete(pet_companion_ui_t *ui,pet_companion_result_t result)
{
    ui->pending=false;ui->feedback=pet_growth_feedback(result.result);
    if(ui->page==PET_SPACE_GAME && (result.result==PET_GROWTH_OK||result.result==PET_GROWTH_DUPLICATE)) {
        ui->game_saved=true;
        ui->feedback=result.result==PET_GROWTH_DUPLICATE?"成绩已保存":result.rewarded?"已保存 · 经验 +15 金币 +3":"练习成绩已保存";
    }
}
void pet_companion_ui_pause(pet_companion_ui_t *ui,uint32_t now)
{
    if(ui->page==PET_SPACE_GAME && ui->game.phase==PET_GAME_RUNNING)pet_game_input(&ui->game,PET_GAME_PAUSE,now);
}
void pet_companion_ui_show(pet_companion_ui_t *ui,const pet_growth_view_t *growth,const pet_companion_view_t *snapshot,uint32_t now)
{
    if(snapshot)ui->snapshot=*snapshot;
    lv_obj_set_y(ui->detail,226);lv_obj_add_flag(ui->canvas,LV_OBJ_FLAG_HIDDEN);
    pet_avatar_tick(&ui->avatar,PET_GROWTH_IDLE,now,false);
    lv_label_set_text(ui->footer,"A 确认  B 下一项\n长按 B 返回 · C 语音");
    lv_label_set_text(ui->detail,"");
    if(ui->page==PET_SPACE_GAME) {
        const pet_game_t *g=&ui->game;
        lv_label_set_text(ui->title,pet_game_name(g->kind));
        lv_label_set_text_fmt(ui->status,"%u 分 · %u 秒",g->score,(unsigned)((pet_game_remaining_ms(g)+999)/1000));
        if(g->phase==PET_GAME_READY) {
            static const char *rules[]={"B 换到下一轨\nA 开启护盾 · 共三次\n接星屑，避开方块\n45 秒自由挑战",
                "先记住图形顺序\nB 选图形，A 确认\n五轮挑战 · 无需声音",
                "等光标靠近金色中线\nA 锁定，B 改变方向\n五次机会 · 最长 60 秒"};
            lv_obj_set_y(ui->detail,90);lv_label_set_text(ui->detail,rules[g->kind]);
            lv_label_set_text(ui->footer,"A 开始 · 长按 B 返回");
        } else if(g->phase==PET_GAME_PAUSED) {
            lv_obj_set_y(ui->detail,116);lv_label_set_text(ui->detail,"游戏已暂停\n时间与成绩保留");
            lv_label_set_text(ui->footer,"A 继续 · 长按 B 退出");
        } else if(g->phase==PET_GAME_RESULT) {
            lv_label_set_text(ui->status,"本局结束");lv_obj_set_y(ui->detail,98);
            lv_label_set_text_fmt(ui->detail,"本局 %u 分\n最佳 %u 分\n%s",g->score,ui->snapshot.state.best[g->kind],
                ui->pending?"正在保存成绩…":ui->feedback?ui->feedback:"A 保存成绩并结算");
            lv_label_set_text(ui->footer,ui->game_saved?"A 再来一局\n长按 B 返回":"A 保存 / 重试\n长按 B 放弃本局成绩");
        } else {
            lv_obj_remove_flag(ui->canvas,LV_OBJ_FLAG_HIDDEN);
            if(g->kind==PET_GAME_STARS)lv_label_set_text_fmt(ui->detail,"护盾 %u · B 换道 A 防护",g->shields);
            else if(g->kind==PET_GAME_SIGNAL)lv_label_set_text_fmt(ui->detail,"第 %u/5 轮 · %s",g->round+1,g->showing?"记住顺序":"B 选择 A 确认");
            else lv_label_set_text_fmt(ui->detail,"第 %u/5 次 · A 锁定",g->round+1);
            lv_label_set_text(ui->footer,"长按 B 暂停 · C 语音");
        }
    } else if(ui->page==PET_SPACE_MEMORIES) {
        lv_label_set_text(ui->title,"共同回忆");lv_label_set_text_fmt(ui->status,"最近 %u 条",count(ui));
        lv_obj_set_y(ui->detail,94);
        if(!count(ui))lv_label_set_text(ui->detail,"从一次摸头开始\n一起留下一段回忆");
        else {
            uint32_t seq=ui->snapshot.state.memory_sequence-ui->selection;
            const pet_memory_t *m=&ui->snapshot.state.memories[seq%8];
            lv_label_set_text_fmt(ui->detail,"%s\n%s\n回忆 #%" PRIu32 "%s",pet_companion_memory_name(m->kind),pet_companion_theme(m->theme),m->sequence,m->epoch?"":" · 未对时");
        }
    } else if(ui->page==PET_SPACE_ATTRIBUTES) {
        lv_label_set_text(ui->title,"伙伴状态");lv_label_set_text(ui->status,"轻松相伴，慢慢成长");lv_obj_set_y(ui->detail,84);
        if(growth)lv_label_set_text_fmt(ui->detail,"等级 %u · 经验 %" PRIu32 "/%" PRIu32 "\n饱食 %u · 精力 %u\n心情 %d · 健康 %u\n亲密 %u · 金币 %" PRIu32 "\n游戏 %u 局 · 专注 %u 次",growth->level,growth->exp,growth->exp_to_next,growth->hunger,growth->energy,growth->mood,growth->health,growth->intimacy,growth->coins,
            (unsigned)ui->snapshot.state.played[0]+ui->snapshot.state.played[1]+ui->snapshot.state.played[2],ui->snapshot.state.focus_completed);
        else lv_label_set_text(ui->detail,"成长服务暂不可用");
    } else {
        pet_appearance_t appearance=ui->snapshot.state.appearance;
        unsigned item=ui->selection+(ui->page==PET_SPACE_FURNITURE && ui->selection?6:0);
        const char *name="";
        if(ui->page==PET_SPACE_WARDROBE) {name="衣橱";appearance.outfit=ui->selection;}
        else if(ui->page==PET_SPACE_THEMES) {name="小舱主题";appearance.theme=ui->selection;}
        else {name="摆件";if(!ui->choose_slot)appearance.slots[ui->slot]=item;}
        lv_label_set_text(ui->title,name);
        pet_avatar_set_appearance(&ui->avatar,&appearance,true);pet_avatar_tick(&ui->avatar,PET_GROWTH_HAPPY,now,true);
        if(ui->page==PET_SPACE_THEMES) {
            lv_label_set_text(ui->status,pet_companion_theme(ui->selection));
            static const char *conditions[]={"默认主题","等级 3 或完成一次专注","完成五局游戏"};
            lv_label_set_text(ui->detail,ui->snapshot.themes&(1u<<ui->selection)?"A 应用主题":conditions[ui->selection]);
        } else if(ui->choose_slot) {
            static const char *positions[]={"左侧地面","右侧地面","墙面搁板"};
            lv_label_set_text(ui->status,positions[ui->selection]);lv_label_set_text(ui->detail,"A 选择这个位置");
        } else {
            const pet_companion_item_t *entry=pet_companion_item(item);
            lv_label_set_text_fmt(ui->status,"%u/7 · %s",ui->selection+1,entry?entry->name:"原始外观");
            if(!entry)lv_label_set_text(ui->detail,"A 恢复 / 清空此位置");
            else if(ui->snapshot.state.owned&(1u<<(item-1)))lv_label_set_text(ui->detail,"已拥有 · A 应用");
            else if(ui->snapshot.eligible&(1u<<(item-1)))lv_label_set_text_fmt(ui->detail,entry->price?"A 兑换 · %u 金币":"A 免费领取",entry->price);
            else lv_label_set_text(ui->detail,entry->requirement);
        }
        if(ui->pending)lv_label_set_text(ui->detail,"正在保存…");
        else if(ui->feedback)lv_label_set_text(ui->detail,ui->feedback);
    }
}
bool pet_companion_ui_tick(pet_companion_ui_t *ui,uint32_t now,bool visible)
{
    if(!visible || ui->page==PET_SPACE_CLOSED) {pet_companion_ui_pause(ui,now);return false;}
    if(ui->page==PET_SPACE_GAME) {
        if(now-ui->frame_ms<40)return false;
        ui->frame_ms=now;pet_game_tick(&ui->game,now);
        lv_obj_invalidate(ui->canvas);return true;
    }
    return pet_avatar_tick(&ui->avatar,PET_GROWTH_HAPPY,now,
        ui->page==PET_SPACE_WARDROBE||ui->page==PET_SPACE_FURNITURE||ui->page==PET_SPACE_THEMES);
}
