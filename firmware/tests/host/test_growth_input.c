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
    /* 出厂配置：无气候扩展、无网络模块；ROOT 导航必须跳过不可用分类。 */
    pet_growth_input_t base = {0};
    pet_growth_input_update(&base, false, false, 0);
    assert(pet_growth_input_selected_action(&base) == PET_MENU_NO_ACTION);
    assert(click(&base, 100, 1000) == -1 && base.menu);
    assert(base.page == PET_MENU_ROOT && base.selection == 0);
    assert(pet_growth_input_selected_action(&base) == PET_MENU_NO_ACTION);
    const pet_menu_page_t *root = pet_menu_page(PET_MENU_ROOT);
    for (unsigned i = 0; i < 6; ++i) { /* 可用分类：互动/状态/消息/设置/系统/返回 */
        const pet_menu_item_t *item = &root->items[base.selection];
        assert(item->page != PET_MENU_ENVIRONMENT);
        assert(item->page != PET_MENU_NETWORK);
        assert(item->page != PET_MENU_AI); /* 语音未启用 */
        pet_growth_input_move(&base, false, 0);
    }
    assert(base.selection == 0); /* 循环一整周回到起点 */
    /* 状态页：激活成就仅预览并关菜单，不执行动作 */
    select_page(&base, PET_MENU_STATUS);
    assert(pet_growth_input_activate(&base, 0) == -1 && base.page == PET_MENU_STATUS);
    seek_action(&base, PET_GROWTH_MENU_ACHIEVEMENTS);
    assert(click(&base, 2000, 1000) == -1 && !base.menu);
    assert(base.page == PET_MENU_ROOT && base.selection == 0);

    /* 启用气候与网络后可进入对应页并激活 */
    pet_growth_input_t full = {.climate_enabled = true, .network_enabled = true};
    pet_growth_input_update(&full, false, false, 0);
    click(&full, 100, 1000);
    select_page(&full, PET_MENU_ENVIRONMENT);
    assert(pet_growth_input_activate(&full, 0) == -1 && full.page == PET_MENU_ENVIRONMENT);
    assert(pet_growth_input_selected_action(&full) == PET_GROWTH_MENU_CLIMATE);
    assert(pet_growth_input_activate(&full, 0) == PET_GROWTH_MENU_CLIMATE && !full.menu);
    click(&full, 1500, 1000);
    select_page(&full, PET_MENU_NETWORK);
    assert(pet_growth_input_activate(&full, 0) == -1 && full.page == PET_MENU_NETWORK);
    assert(pet_growth_input_selected_action(&full) == PET_GROWTH_MENU_NETWORK);
    assert(pet_growth_input_activate(&full, 0) == PET_GROWTH_MENU_NETWORK && !full.menu);

    /* 左键：子页回 ROOT，ROOT 关菜单 */
    assert(pet_growth_input_navigate(&full, PET_NAV_RIGHT, false, 2000) == -1 && full.menu);
    assert(full.page == PET_MENU_ROOT);
    select_page(&full, PET_MENU_MESSAGES);
    assert(pet_growth_input_activate(&full, 0) == -1 && full.page == PET_MENU_MESSAGES);
    assert(pet_growth_input_selected_action(&full) == PET_MENU_MESSAGES_BROWSE);
    assert(pet_growth_input_navigate(&full, PET_NAV_LEFT, false, 2100) == -1);
    assert(full.menu && full.page == PET_MENU_ROOT && full.selection == 0);
    assert(pet_growth_input_navigate(&full, PET_NAV_LEFT, false, 2200) == -1 && !full.menu);

    /* RETURN 关闭菜单并返回其动作 id；系统页 ABOUT 返回版本动作 */
    click(&full, 3000, 1000);
    seek_action(&full, PET_GROWTH_MENU_RETURN);
    assert(pet_growth_input_activate(&full, 0) == PET_GROWTH_MENU_RETURN && !full.menu);
    click(&full, 4000, 1000);
    select_page(&full, PET_MENU_SYSTEM);
    assert(pet_growth_input_activate(&full, 0) == -1 && full.page == PET_MENU_SYSTEM);
    assert(pet_growth_input_selected_action(&full) == PET_MENU_ABOUT);
    assert(pet_growth_input_activate(&full, 0) == PET_MENU_ABOUT && !full.menu);

    /* 设置页首个条目可直接激活 */
    click(&full, 5000, 1000);
    select_page(&full, PET_MENU_SETTINGS);
    assert(pet_growth_input_activate(&full, 0) == -1 && full.page == PET_MENU_SETTINGS);
    assert(pet_growth_input_selected_action(&full) == PET_MENU_VOLUME_UP);
    assert(pet_growth_input_activate(&full, 0) == PET_MENU_VOLUME_UP && !full.menu);

    /* 30s 无操作自动关菜单；进入子页刷新计时 */
    pet_growth_input_t timeout = {.climate_enabled = true};
    pet_growth_input_update(&timeout, false, false, 0);
    click(&timeout, 100, 1000);
    assert(timeout.menu);
    assert(!pet_growth_input_menu_expired(&timeout, 1000 + 29999));
    assert(pet_growth_input_activate(&timeout, 40000) == -1); /* 进入子页刷新计时 */
    assert(timeout.page == PET_MENU_INTERACT);
    assert(!pet_growth_input_menu_expired(&timeout, 69000));
    assert(pet_growth_input_menu_expired(&timeout, 70000));
    assert(!timeout.menu && timeout.page == PET_MENU_ROOT && timeout.selection == 0);

    /* voice_only：开机长按直达 AI 页，TALK 只能按住触发 */
    pet_growth_input_t voice = {.voice_enabled = true, .voice_only = true};
    pet_growth_input_update(&voice, false, false, 0);
    click(&voice, 100, 1000);
    assert(voice.menu && voice.page == PET_MENU_AI && voice.selection == 0);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_TALK);
    assert(pet_growth_input_navigate(&voice, PET_NAV_RIGHT, false, 1300) == -1);
    assert(voice.menu && !voice.holding);
    click(&voice, 1500, 100);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_MUTE);
    assert(click(&voice, 1700, 1000) == PET_GROWTH_MENU_AI_MUTE && !voice.menu);
    click(&voice, 2000, 1000);
    assert(voice.menu && voice.page == PET_MENU_AI && voice.selection == 0);
    click(&voice, 2200, 100);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_MUTE);
    click(&voice, 2400, 100);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_DND);
    click(&voice, 2600, 100);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_CLEAR);
    click(&voice, 2800, 100);
    assert(pet_growth_input_selected_action(&voice) == PET_GROWTH_MENU_AI_TALK);
    /* 按住 TALK 800ms 触发 PTT */
    assert(pet_growth_input_update(&voice, true, false, 3000) == -1);
    assert(pet_growth_input_update(&voice, true, false, 3030) == -1);
    assert(pet_growth_input_update(&voice, true, false, 3799) == -1);
    assert(pet_growth_input_update(&voice, true, false, 3800) == PET_GROWTH_MENU_AI_TALK);
    assert(voice.holding && !voice.menu);
    assert(pet_growth_input_update(&voice, true, false, 3900) == -1);
    pet_growth_input_update(&voice, false, false, 3950);
    assert(pet_growth_input_update(&voice, false, false, 3980) == PET_GROWTH_INPUT_AI_RELEASE);
    assert(!voice.holding);
    /* 语音面板打开时短按 = 取消手势；长按重新打开菜单并回到 AI 页 */
    voice.voice_active = true;
    assert(click(&voice, 4200, 100) == PET_GROWTH_INPUT_AI_CANCEL);
    click(&voice, 4400, 1000);
    assert(voice.menu && voice.page == PET_MENU_AI);
    voice.voice_active = false;

    /* 非 voice_only 且语音就绪：长按仍直达 AI 页；左键先回 ROOT 再退出 */
    pet_growth_input_t ready = {.voice_enabled = true, .voice_ready = true,
        .climate_enabled = true, .network_enabled = true};
    pet_growth_input_update(&ready, false, false, 0);
    click(&ready, 100, 1000);
    assert(ready.page == PET_MENU_AI && ready.selection == 0);
    assert(pet_growth_input_navigate(&ready, PET_NAV_LEFT, false, 1200) == -1);
    assert(ready.menu && ready.page == PET_MENU_ROOT);
    assert(pet_growth_input_navigate(&ready, PET_NAV_LEFT, false, 1300) == -1 && !ready.menu);
    /* RIGHT 打开菜单（语音就绪 → AI 页）；LEFT 回 ROOT；UP 移动到返回主页 */
    assert(pet_growth_input_navigate(&ready, PET_NAV_RIGHT, false, 1400) == -1 && ready.menu);
    assert(ready.page == PET_MENU_AI && ready.selection == 0);
    assert(pet_growth_input_navigate(&ready, PET_NAV_LEFT, false, 1450) == -1);
    assert(ready.page == PET_MENU_ROOT);
    pet_growth_input_navigate(&ready, PET_NAV_UP, false, 1500);
    assert(pet_growth_input_selected_action(&ready) == PET_GROWTH_MENU_RETURN);
    /* 阻塞输入强制关菜单并取消手势 */
    pet_growth_input_update(&ready, true, false, 1600);
    pet_growth_input_update(&ready, true, false, 1630);
    pet_growth_input_update(&ready, true, true, 2000);
    pet_growth_input_update(&ready, false, false, 2800);
    assert(pet_growth_input_update(&ready, false, false, 2830) == -1 && !ready.menu);

    /* 开机按住不得执行任何动作 */
    pet_growth_input_t boot = {.climate_enabled = true};
    pet_growth_input_update(&boot, true, false, 0);
    pet_growth_input_update(&boot, false, false, 2000);
    assert(pet_growth_input_update(&boot, false, false, 2030) == -1 && !boot.menu);

    puts("growth input: two-level menu navigation, PTT, timeout and gating passed");
    return 0;
}
