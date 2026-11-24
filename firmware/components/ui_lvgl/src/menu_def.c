#include "menu_def.h"

static const pet_menu_item_t root_items[] = {
    {PET_MENU_NO_ACTION, PET_MENU_INTERACT, "互动"},
    {PET_MENU_NO_ACTION, PET_MENU_PLAYGROUND, "游乐场"},
    {PET_MENU_NO_ACTION, PET_MENU_CABIN, "小舱"},
    {PET_MENU_NO_ACTION, PET_MENU_STATUS, "成长"},
    {PET_MENU_NO_ACTION, PET_MENU_COMPANION, "陪伴"},
    {PET_MENU_NO_ACTION, PET_MENU_SETTINGS, "设置"},
};
static const pet_menu_item_t playground_items[] = {
    {PET_MENU_GAME_STARS, PET_MENU_NO_PAGE, "接星屑"},
    {PET_MENU_GAME_SIGNAL, PET_MENU_NO_PAGE, "信号接力"},
    {PET_MENU_GAME_ORBIT, PET_MENU_NO_PAGE, "轨道对接"},
};
static const pet_menu_item_t cabin_items[] = {
    {PET_MENU_WARDROBE, PET_MENU_NO_PAGE, "衣橱"},
    {PET_MENU_FURNITURE, PET_MENU_NO_PAGE, "摆件"},
    {PET_MENU_THEMES, PET_MENU_NO_PAGE, "主题"},
};
static const pet_menu_item_t companion_items[] = {
    {PET_GROWTH_MENU_AI_TALK, PET_MENU_NO_PAGE, "语音对话"},
    {PET_GROWTH_MENU_FOCUS, PET_MENU_NO_PAGE, "开始专注"},
    {PET_GROWTH_MENU_FOCUS_PAUSE, PET_MENU_NO_PAGE, "暂停专注"},
    {PET_GROWTH_MENU_FOCUS_RESUME, PET_MENU_NO_PAGE, "继续专注"},
    {PET_GROWTH_MENU_FOCUS_CANCEL, PET_MENU_NO_PAGE, "取消专注"},
    {PET_MENU_MESSAGES_BROWSE, PET_MENU_NO_PAGE, "未读消息"},
    {PET_GROWTH_MENU_CLIMATE, PET_MENU_NO_PAGE, "温湿度"},
};
static const pet_menu_item_t interact_items[] = {
    {PET_GROWTH_FEED, PET_MENU_NO_PAGE, "喂食"},
    {PET_GROWTH_PET, PET_MENU_NO_PAGE, "摸头"},
    {PET_GROWTH_SLEEP, PET_MENU_NO_PAGE, "睡觉"},
    {PET_GROWTH_WAKE, PET_MENU_NO_PAGE, "唤醒"},
};
static const pet_menu_item_t status_items[] = {
    {PET_MENU_ATTRIBUTES, PET_MENU_NO_PAGE, "伙伴状态"},
    {PET_MENU_MEMORIES, PET_MENU_NO_PAGE, "共同回忆"},
    {PET_GROWTH_CLAIM_FEED, PET_MENU_NO_PAGE, "喂食任务"},
    {PET_GROWTH_CLAIM_PET, PET_MENU_NO_PAGE, "摸头任务"},
    {PET_GROWTH_CLAIM_PLAY, PET_MENU_NO_PAGE, "玩耍任务"},
    {PET_GROWTH_MENU_ACHIEVEMENTS, PET_MENU_NO_PAGE, "成长成就"},
};
static const pet_menu_item_t environment_items[] = {
    {PET_GROWTH_MENU_CLIMATE, PET_MENU_NO_PAGE, "温湿度"},
};
static const pet_menu_item_t messages_items[] = {
    {PET_MENU_MESSAGES_BROWSE, PET_MENU_NO_PAGE, "未读消息"},
};
static const pet_menu_item_t ai_items[] = {
    {PET_GROWTH_MENU_AI_TALK, PET_MENU_NO_PAGE, "按住说话"},
    {PET_GROWTH_MENU_AI_MUTE, PET_MENU_NO_PAGE, "语音静音"},
    {PET_GROWTH_MENU_AI_DND, PET_MENU_NO_PAGE, "免打扰"},
    {PET_GROWTH_MENU_AI_CLEAR, PET_MENU_NO_PAGE, "清除对话"},
};
static const pet_menu_item_t settings_items[] = {
    {PET_MENU_VOLUME_UP, PET_MENU_NO_PAGE, "音量 +"},
    {PET_MENU_VOLUME_DOWN, PET_MENU_NO_PAGE, "音量 -"},
    {PET_MENU_BRIGHTNESS_UP, PET_MENU_NO_PAGE, "亮度 +"},
    {PET_MENU_BRIGHTNESS_DOWN, PET_MENU_NO_PAGE, "亮度 -"},
    {PET_MENU_SFX_UP, PET_MENU_NO_PAGE, "提示音 +"},
    {PET_MENU_SFX_DOWN, PET_MENU_NO_PAGE, "提示音 -"},
    {PET_MENU_SCREEN_TIMEOUT, PET_MENU_NO_PAGE, "息屏时长"},
    {PET_MENU_IDLE_BRIGHTNESS, PET_MENU_NO_PAGE, "待机亮度"},
    {PET_GROWTH_MENU_AI_MUTE, PET_MENU_NO_PAGE, "语音静音"},
    {PET_GROWTH_MENU_AI_DND, PET_MENU_NO_PAGE, "免打扰"},
    {PET_GROWTH_MENU_AI_CLEAR, PET_MENU_NO_PAGE, "清除对话"},
    {PET_GROWTH_MENU_NETWORK, PET_MENU_NO_PAGE, "网络设置"},
    {PET_MENU_ABOUT, PET_MENU_NO_PAGE, "关于版本"},
};
static const pet_menu_item_t network_items[] = {
    {PET_GROWTH_MENU_NETWORK, PET_MENU_NO_PAGE, "网络设置"},
};
static const pet_menu_item_t system_items[] = {
    {PET_MENU_ABOUT, PET_MENU_NO_PAGE, "关于版本"},
};

static const pet_menu_page_t pages[PET_MENU_PAGE_COUNT] = {
    [PET_MENU_PLAYGROUND] = {"游乐场", playground_items, sizeof(playground_items)/sizeof(playground_items[0])},
    [PET_MENU_CABIN] = {"小舱", cabin_items, sizeof(cabin_items)/sizeof(cabin_items[0])},
    [PET_MENU_COMPANION] = {"陪伴", companion_items, sizeof(companion_items)/sizeof(companion_items[0])},
    [PET_MENU_ROOT] = {"主菜单", root_items, sizeof(root_items) / sizeof(root_items[0])},
    [PET_MENU_INTERACT] = {"互动", interact_items, sizeof(interact_items) / sizeof(interact_items[0])},
    [PET_MENU_STATUS] = {"成长", status_items, sizeof(status_items) / sizeof(status_items[0])},
    [PET_MENU_ENVIRONMENT] = {"环境", environment_items, sizeof(environment_items) / sizeof(environment_items[0])},
    [PET_MENU_MESSAGES] = {"消息", messages_items, sizeof(messages_items) / sizeof(messages_items[0])},
    [PET_MENU_AI] = {"AI 对话", ai_items, sizeof(ai_items) / sizeof(ai_items[0])},
    [PET_MENU_SETTINGS] = {"设置", settings_items, sizeof(settings_items) / sizeof(settings_items[0])},
    [PET_MENU_NETWORK] = {"网络", network_items, sizeof(network_items) / sizeof(network_items[0])},
    [PET_MENU_SYSTEM] = {"系统", system_items, sizeof(system_items) / sizeof(system_items[0])},
};

const pet_menu_page_t *pet_menu_page(uint8_t page)
{
    return page < PET_MENU_PAGE_COUNT ? &pages[page] : NULL;
}
