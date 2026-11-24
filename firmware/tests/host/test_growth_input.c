#include <assert.h>
#include <stdio.h>
#include "growth_input.h"

static int click(pet_growth_input_t *p, uint32_t start, uint32_t duration)
{
    assert(pet_growth_input_update(p, true, false, start) == -1);
    assert(pet_growth_input_update(p, true, false, start + 30) == -1);
    assert(pet_growth_input_update(p, false, false, start + duration) == -1);
    return pet_growth_input_update(p, false, false, start + duration + 30);
}

/* 把菜单移动到指定动作；要求该动作在当前页存在且可用。 */
static void seek_action(pet_growth_input_t *p, uint8_t action)
{
    const pet_menu_page_t *page = pet_menu_page(p->page);
    for (unsigned guard = 0; guard <= page->count; ++guard) {
        if (pet_growth_input_selected_action(p) == action) return;
        pet_growth_input_move(p, false, 0);
    }
    assert(!"action not reachable");
}

/* 在 ROOT 页移动到指定分类；分类不可用时不可能到达。 */
static void select_page(pet_growth_input_t *p, uint8_t page_id)
{
    const pet_menu_page_t *root = pet_menu_page(PET_MENU_ROOT);
    for (unsigned guard = 0; guard <= root->count; ++guard) {
        if (root->items[p->selection].page == page_id) return;
        pet_growth_input_move(p, false, 0);
    }
    assert(!"page not reachable");
}

int main(void)
{
    pet_growth_input_t p={.voice_enabled=true,.voice_ready=true,.network_enabled=true,.climate_enabled=true};
    pet_growth_input_update(&p,false,false,0);
    assert(click(&p,100,1000)==-1 && p.menu && p.page==PET_MENU_ROOT);
    select_page(&p,PET_MENU_SETTINGS);
    assert(pet_growth_input_activate(&p,1200)==-1);
    seek_action(&p,PET_MENU_BRIGHTNESS_DOWN);
    unsigned selected=p.selection;
    assert(pet_growth_input_activate(&p,1300)==PET_MENU_BRIGHTNESS_DOWN);
    assert(p.menu && p.selection==selected); /* 连续调节无需重进菜单。 */
    pet_growth_input_navigate(&p,PET_NAV_LEFT,false,1400);
    assert(p.menu && p.page==PET_MENU_ROOT && p.selection==5);
    pet_growth_input_activate(&p,1500);
    assert(p.page==PET_MENU_SETTINGS && p.selection==selected);
    pet_growth_input_navigate(&p,PET_NAV_LEFT,false,1600);
    select_page(&p,PET_MENU_COMPANION);pet_growth_input_activate(&p,1700);
    seek_action(&p,PET_GROWTH_MENU_CLIMATE);
    assert(pet_growth_input_activate(&p,1800)==PET_GROWTH_MENU_CLIMATE && !p.menu);
    pet_growth_input_open_menu(&p,1900);select_page(&p,PET_MENU_SETTINGS);pet_growth_input_activate(&p,2000);
    seek_action(&p,PET_GROWTH_MENU_NETWORK);
    assert(pet_growth_input_activate(&p,2100)==PET_GROWTH_MENU_NETWORK);
    pet_growth_input_open_menu(&p,2200);select_page(&p,PET_MENU_PLAYGROUND);pet_growth_input_activate(&p,2300);
    seek_action(&p,PET_MENU_GAME_SIGNAL);
    assert(pet_growth_input_activate(&p,2400)==PET_MENU_GAME_SIGNAL && !p.menu);
    pet_growth_input_open_menu(&p,2500);select_page(&p,PET_MENU_STATUS);pet_growth_input_activate(&p,2600);
    seek_action(&p,PET_GROWTH_MENU_ACHIEVEMENTS);
    assert(pet_growth_input_activate(&p,2700)==-1 && p.menu);
    assert(!pet_growth_input_menu_expired(&p,32699));
    assert(pet_growth_input_menu_expired(&p,32700) && !p.menu);

    pet_growth_input_t base={0};pet_growth_input_update(&base,false,false,0);
    pet_growth_input_open_menu(&base,1);select_page(&base,PET_MENU_COMPANION);pet_growth_input_activate(&base,2);
    const pet_menu_page_t *page=pet_menu_page(base.page);
    for(unsigned i=0;i<page->count;++i) {
        if(page->items[i].action==PET_GROWTH_MENU_CLIMATE||page->items[i].action==PET_GROWTH_MENU_AI_TALK)
            assert(!pet_growth_input_item_available(&base,&page->items[i]));
    }
    pet_growth_input_t voice={.voice_enabled=true,.voice_only=true};
    pet_growth_input_update(&voice,false,false,0);click(&voice,100,1000);
    assert(voice.page==PET_MENU_AI);
    assert(pet_growth_input_activate(&voice,1200)==-1); /* Direction cannot latch PTT. */
    assert(pet_growth_input_update(&voice,true,false,2000)==-1);
    assert(pet_growth_input_update(&voice,true,false,2030)==-1);
    assert(pet_growth_input_update(&voice,true,false,2799)==-1);
    assert(pet_growth_input_update(&voice,true,false,2800)==PET_GROWTH_MENU_AI_TALK);
    assert(voice.holding && !voice.menu);
    pet_growth_input_update(&voice,false,false,3000);
    assert(pet_growth_input_update(&voice,false,false,3030)==PET_GROWTH_INPUT_AI_RELEASE);
    voice.voice_active=true;
    assert(click(&voice,3200,100)==PET_GROWTH_INPUT_AI_CANCEL);
    pet_growth_input_update(&voice,true,true,4000);
    pet_growth_input_update(&voice,false,false,5000);
    assert(pet_growth_input_update(&voice,false,false,5030)==-1 && !voice.menu);
    pet_growth_input_t boot={0};pet_growth_input_update(&boot,true,false,0);
    pet_growth_input_update(&boot,false,false,2000);
    assert(pet_growth_input_update(&boot,false,false,2030)==-1 && !boot.menu);
    puts("growth input: six categories, remembered selection, setting edits, gates, PTT and rearm passed");
}
