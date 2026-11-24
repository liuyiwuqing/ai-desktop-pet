#pragma once
#include <stdint.h>
#include "pet/growth.h"

/* 两级菜单静态表（docs/30 §4.1）：深度 ≤2，新增节点只改表不改状态机。
 * 动作 id 复用成长动作 0..PET_GROWTH_ACTION_COUNT-1 与既有菜单动作；
 * RETURN 关闭菜单回主页，ACHIEVEMENTS 仅预览（激活只关菜单），
 * AI_TALK 由长按 PTT 语义处理，activate 不返回该动作。 */
enum {
    PET_GROWTH_MENU_ACHIEVEMENTS = PET_GROWTH_ACTION_COUNT,
    PET_GROWTH_MENU_CLIMATE,
    PET_GROWTH_MENU_FOCUS,
    PET_GROWTH_MENU_FOCUS_PAUSE,
    PET_GROWTH_MENU_FOCUS_RESUME,
    PET_GROWTH_MENU_FOCUS_CANCEL,
    PET_GROWTH_MENU_NETWORK,
    PET_GROWTH_MENU_RETURN,
    PET_GROWTH_MENU_COUNT,
    PET_GROWTH_MENU_AI_TALK = PET_GROWTH_MENU_COUNT,
    PET_GROWTH_MENU_AI_MUTE,
    PET_GROWTH_MENU_AI_DND,
    PET_GROWTH_MENU_AI_CLEAR,
    PET_MENU_MESSAGES_BROWSE,
    PET_MENU_VOLUME_UP,
    PET_MENU_VOLUME_DOWN,
    PET_MENU_BRIGHTNESS_UP,
    PET_MENU_BRIGHTNESS_DOWN,
    PET_MENU_SFX_UP,
    PET_MENU_SFX_DOWN,
    PET_MENU_SCREEN_TIMEOUT,
    PET_MENU_IDLE_BRIGHTNESS,
    PET_MENU_ABOUT,
    PET_MENU_GAME_STARS, PET_MENU_GAME_SIGNAL, PET_MENU_GAME_ORBIT,
    PET_MENU_WARDROBE, PET_MENU_FURNITURE, PET_MENU_THEMES,
    PET_MENU_MEMORIES, PET_MENU_ATTRIBUTES,
    PET_MENU_ACTION_COUNT,
};

typedef enum {
    PET_MENU_ROOT = 0,
    PET_MENU_INTERACT,
    PET_MENU_STATUS,
    PET_MENU_ENVIRONMENT,
    PET_MENU_MESSAGES,
    PET_MENU_AI,
    PET_MENU_SETTINGS,
    PET_MENU_NETWORK,
    PET_MENU_SYSTEM,
    PET_MENU_PLAYGROUND, PET_MENU_CABIN, PET_MENU_COMPANION,
    PET_MENU_PAGE_COUNT,
} pet_menu_page_id_t;

#define PET_MENU_NO_PAGE 0xFFu
#define PET_MENU_NO_ACTION 0xFFu

typedef struct {
    uint8_t action; /* 叶子的动作 id */
    uint8_t page;   /* 子页 pet_menu_page_id_t；PET_MENU_NO_PAGE=叶子 */
    const char *label;
} pet_menu_item_t;

typedef struct {
    const char *title;
    const pet_menu_item_t *items;
    uint8_t count;
} pet_menu_page_t;

const pet_menu_page_t *pet_menu_page(uint8_t page);
