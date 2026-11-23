#include <assert.h>
#include <stdio.h>
#include "menu_def.h"

int main(void)
{
    /* 根页：8 个分类 + 返回主页叶子；分类页不再嵌套子页，菜单深度恒为 2。 */
    const pet_menu_page_t *root = pet_menu_page(PET_MENU_ROOT);
    assert(root && root->title && root->count == 9);
    for (unsigned i = 0; i < root->count; ++i) {
        const pet_menu_item_t *item = &root->items[i];
        assert(item->label && *item->label);
        if (i + 1 < root->count) {
            assert(item->page != PET_MENU_NO_PAGE);
            assert(item->action == PET_MENU_NO_ACTION);
        } else {
            assert(item->page == PET_MENU_NO_PAGE);
            assert(item->action == PET_GROWTH_MENU_RETURN);
        }
    }
    for (unsigned page = 1; page < PET_MENU_PAGE_COUNT; ++page) {
        const pet_menu_page_t *pg = pet_menu_page((uint8_t)page);
        assert(pg && pg->title && pg->count > 0);
        for (unsigned i = 0; i < pg->count; ++i) {
            assert(pg->items[i].page == PET_MENU_NO_PAGE);
            assert(pg->items[i].action != PET_MENU_NO_ACTION);
            assert(pg->items[i].label && *pg->items[i].label);
            if (page != PET_MENU_SYSTEM)
                assert(pg->items[i].action < PET_MENU_ABOUT);
        }
    }
    /* 关键页内容固定：AI 页四项语音动作、设置页 8 个调整项、系统页仅版本。 */
    const pet_menu_page_t *ai = pet_menu_page(PET_MENU_AI);
    assert(ai->count == 4);
    assert(ai->items[0].action == PET_GROWTH_MENU_AI_TALK);
    assert(ai->items[1].action == PET_GROWTH_MENU_AI_MUTE);
    assert(ai->items[2].action == PET_GROWTH_MENU_AI_DND);
    assert(ai->items[3].action == PET_GROWTH_MENU_AI_CLEAR);
    assert(pet_menu_page(PET_MENU_SETTINGS)->count == 8);
    assert(pet_menu_page(PET_MENU_SETTINGS)->items[0].action == PET_MENU_VOLUME_UP);
    assert(pet_menu_page(PET_MENU_SETTINGS)->items[6].action == PET_MENU_SCREEN_TIMEOUT);
    assert(pet_menu_page(PET_MENU_SYSTEM)->count == 1);
    assert(pet_menu_page(PET_MENU_SYSTEM)->items[0].action == PET_MENU_ABOUT);
    /* 页内动作互不重复，保证选择无歧义。 */
    for (unsigned page = 0; page < PET_MENU_PAGE_COUNT; ++page) {
        const pet_menu_page_t *pg = pet_menu_page((uint8_t)page);
        for (unsigned i = 0; i < pg->count; ++i)
            for (unsigned j = i + 1; j < pg->count; ++j)
                assert(pg->items[i].action != pg->items[j].action ||
                       pg->items[i].page != pg->items[j].page);
    }
    assert(pet_menu_page(0xFF) == NULL);
    puts("menu def: page tables, leaf actions and depth bounds passed");
    return 0;
}
