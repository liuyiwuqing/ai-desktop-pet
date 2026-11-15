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
int main(void)
{
    /* The release device has no climate extension; navigation must skip it. */
    pet_growth_input_t base = {0};
    pet_growth_input_update(&base, false, false, 0);
    click(&base, 100, 1000);
    for (unsigned i = 0; i < PET_GROWTH_MENU_CLIMATE; ++i) {
        click(&base, 1300 + i * 200, 100);
        assert(base.selection != PET_GROWTH_MENU_CLIMATE);
    }
    assert(base.selection == PET_GROWTH_MENU_FOCUS);
    assert(pet_growth_input_navigate(&base, PET_NAV_LEFT, false, 4000) == -1 && !base.menu);
    assert(pet_growth_input_navigate(&base, PET_NAV_RIGHT, false, 4100) == -1 && base.menu);
    pet_growth_input_navigate(&base, PET_NAV_DOWN, false, 4200);
    assert(base.selection == PET_GROWTH_PET);
    assert(pet_growth_input_navigate(&base, PET_NAV_RIGHT, false, 4300) == PET_GROWTH_PET && !base.menu);

    base.menu=true;base.selection=PET_GROWTH_MENU_FOCUS_CANCEL;
    pet_growth_input_navigate(&base,PET_NAV_DOWN,false,4400);
    assert(base.selection==PET_GROWTH_MENU_RETURN);
    base.network_enabled=true;base.selection=PET_GROWTH_MENU_FOCUS_CANCEL;
    pet_growth_input_navigate(&base,PET_NAV_DOWN,false,4500);
    assert(base.selection==PET_GROWTH_MENU_NETWORK);
    assert(pet_growth_input_navigate(&base,PET_NAV_RIGHT,false,4600)==PET_GROWTH_MENU_NETWORK&&!base.menu);

    pet_growth_input_t p = {.climate_enabled = true, .network_enabled = true};
    pet_growth_input_update(&p, false, false, 0);
    assert(click(&p, 100, 100) == -1 && !p.menu);
    assert(click(&p, 300, 1000) == -1 && p.menu && p.selection == 0);
    assert(click(&p, 1500, 100) == -1 && p.selection == 1);
    assert(click(&p, 1700, 1000) == 1 && !p.menu);
    click(&p, 3000, 1000);
    pet_growth_input_update(&p, true, false, 4200);
    pet_growth_input_update(&p, true, true, 4500); /* 消息抢占取消手势 */
    pet_growth_input_update(&p, false, false, 5300);
    assert(pet_growth_input_update(&p, false, false, 5330) == -1 && !p.menu);
    p = (pet_growth_input_t){.climate_enabled = true, .network_enabled = true};
    pet_growth_input_update(&p, true, false, 0); /* 开机按住不得执行 */
    pet_growth_input_update(&p, false, false, 2000);
    assert(pet_growth_input_update(&p, false, false, 2030) == -1 && !p.menu);
    click(&p, 3000, 1000);
    for (unsigned i=0;i<8;i++) click(&p, 4200+i*200,100);
    assert(p.selection == 8);
    assert(p.selection == PET_GROWTH_MENU_ACHIEVEMENTS);
    assert(click(&p, 6000,1000) == -1 && !p.menu);
    click(&p, 7200, 1000);
    for (unsigned i = 0; i < PET_GROWTH_MENU_RETURN; ++i) click(&p, 8400 + i * 200, 100);
    assert(p.selection == PET_GROWTH_MENU_RETURN);
    assert(click(&p, 12000, 1000) == -1 && !p.menu);
    click(&p, 13400, 1000);
    for (unsigned i = 0; i < PET_GROWTH_MENU_COUNT; ++i) click(&p, 14600 + i * 200, 100);
    assert(p.selection == 0 && p.menu);
    pet_growth_input_update(&p, false, true, 20000);
    assert(!p.menu);
    p = (pet_growth_input_t){.voice_enabled = true, .voice_only = true};
    pet_growth_input_update(&p, false, false, 0);
    click(&p, 100, 1000);
    assert(p.menu && p.selection == PET_GROWTH_MENU_AI_TALK);
    pet_growth_input_update(&p, true, false, 1300);
    pet_growth_input_update(&p, true, false, 1330);
    assert(pet_growth_input_update(&p, true, false, 2099) == -1);
    assert(pet_growth_input_update(&p, true, false, 2100) == PET_GROWTH_MENU_AI_TALK);
    assert(p.holding && !p.menu);
    assert(pet_growth_input_update(&p, true, false, 2200) == -1);
    pet_growth_input_update(&p, false, false, 2300);
    assert(pet_growth_input_update(&p, false, false, 2330) == PET_GROWTH_INPUT_AI_RELEASE);
    assert(!p.holding);
    p.voice_active = true;
    assert(click(&p, 2500, 100) == PET_GROWTH_INPUT_AI_CANCEL);
    click(&p, 2800, 1000);
    assert(p.menu && p.selection == PET_GROWTH_MENU_AI_TALK);
    click(&p, 4000, 100);
    assert(p.selection == PET_GROWTH_MENU_AI_MUTE);
    assert(click(&p, 4200, 1000) == PET_GROWTH_MENU_AI_MUTE);
    click(&p, 5500, 1000);
    pet_growth_input_update(&p, true, false, 6800);
    pet_growth_input_update(&p, true, false, 6830);
    assert(pet_growth_input_update(&p, true, false, 7600) == PET_GROWTH_MENU_AI_TALK);
    assert(pet_growth_input_update(&p, true, true, 7650) == PET_GROWTH_INPUT_AI_CANCEL);
    assert(!p.press_voice);
    pet_growth_input_update(&p, false, false, 7800);
    assert(pet_growth_input_update(&p, false, false, 7830) == -1);
    p = (pet_growth_input_t){.voice_enabled = true, .voice_ready = true, .climate_enabled = true, .network_enabled = true};
    pet_growth_input_update(&p, false, false, 0);
    click(&p, 100, 1000);
    assert(p.selection == PET_GROWTH_MENU_AI_TALK);
    click(&p, 1300, 100);
    click(&p, 1500, 100);
    assert(p.selection == PET_GROWTH_MENU_AI_DND);
    click(&p,1700,100);
    assert(p.selection==PET_GROWTH_MENU_AI_CLEAR);
    click(&p,1900,100);
    assert(p.selection==PET_GROWTH_FEED);
    for (unsigned i = 0; i < PET_GROWTH_MENU_RETURN; ++i) click(&p, 2100 + i * 200, 100);
    assert(p.selection == PET_GROWTH_MENU_RETURN);
    click(&p,6000,100);
    assert(p.selection==PET_GROWTH_MENU_AI_TALK);
    /* Direction confirm cannot open the microphone; a fresh hold is required. */
    assert(pet_growth_input_navigate(&p, PET_NAV_RIGHT, false, 6300) == -1 && p.menu && !p.holding);
    assert(pet_growth_input_navigate(&p, PET_NAV_LEFT, false, 6400) == -1 && !p.menu);
    p = (pet_growth_input_t){.voice_enabled = true, .voice_only = true};
    pet_growth_input_navigate(&p, PET_NAV_RIGHT, false, 6500);
    pet_growth_input_navigate(&p, PET_NAV_UP, false, 6600);
    assert(p.selection == PET_GROWTH_MENU_RETURN);
    pet_growth_input_navigate(&p, PET_NAV_UP, false, 6700);
    assert(p.selection == PET_GROWTH_MENU_AI_CLEAR);
    pet_growth_input_navigate(&p, PET_NAV_DOWN, false, 6800);
    assert(p.selection == PET_GROWTH_MENU_RETURN);
    pet_growth_input_navigate(&p, PET_NAV_RIGHT, true, 6900);
    assert(!p.menu);
    puts("growth input: navigation, submit, cancel, preemption and held boot passed");
}
