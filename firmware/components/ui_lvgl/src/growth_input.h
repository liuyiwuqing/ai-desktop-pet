#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "menu_def.h"
#include "pet/growth.h"
#include "pet/hal/navigation.h"

/* 输入手势返回码（非菜单动作）：松开结束 PTT / 取消语音。 */
enum {
    PET_GROWTH_INPUT_AI_RELEASE = PET_MENU_ABOUT + 1,
    PET_GROWTH_INPUT_AI_CANCEL,
};

/* 菜单可用性按动作 id 门控：AI 项需 voice_enabled、环境需 climate_enabled、
 * 网络需 network_enabled、成长/成就/专注需 !voice_only；页面级门控为
 * “页内存在任一可用条目”。 */
typedef struct {
    uint32_t changed_ms, pressed_ms, last_menu_ms;
    uint8_t selection; /* 当前页内条目索引 */
    uint8_t page;      /* pet_menu_page_id_t；菜单关闭时为 PET_MENU_ROOT */
    bool initialized, raw, stable, armed, menu;
    bool voice_enabled, voice_only, voice_active, voice_ready;
    bool climate_enabled;
    bool network_enabled;
    bool navigation_enabled;
    bool holding, press_voice;
} pet_growth_input_t;
/* AI 项复用同一个按键代次；被消息/显示故障遮挡后，必须重新松开再按。 */
int pet_growth_input_update(pet_growth_input_t *input, bool pressed, bool blocked, uint32_t now);
int pet_growth_input_navigate(pet_growth_input_t *input, pet_navigation_t direction,
                             bool blocked, uint32_t now);
/* 当前选中条目的动作 id；菜单关闭或选中分类页时返回 PET_MENU_NO_ACTION。 */
uint8_t pet_growth_input_selected_action(const pet_growth_input_t *input);
bool pet_growth_input_open_menu(pet_growth_input_t *input, uint32_t now);
void pet_growth_input_move(pet_growth_input_t *input, bool backwards, uint32_t now);
/* 叶子返回动作 id（RETURN 亦返回其 id 供调用方收尾）；查看项/子页返回 -1；
 * AI_TALK 只能通过按住 PTT 触发。 */
int pet_growth_input_activate(pet_growth_input_t *input, uint32_t now);
/* 菜单 30s 无操作自动关闭；返回 true 表示本次发生了超时关闭。 */
bool pet_growth_input_menu_expired(pet_growth_input_t *input, uint32_t now);
