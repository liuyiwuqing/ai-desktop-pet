#include "key_actions.h"

/* docs/30 §3.3 场景 × 按键映射矩阵；未列出的单元格一律 NONE。
 * 设计原则：A=确认/播放控制，B=导航/音量，C=语音/静音；KEY_C 长按恒为静音。 */
pet_ui_action_t pet_ui_key_action(pet_ui_scene_t scene, pet_key_event_kind_t kind, pet_key_id_t key)
{
    switch (scene) {
    case PET_UI_SCENE_HOME:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_OPEN_MENU : key == PET_KEY_NEXT
                ? PET_UI_ACTION_PAGE_NEXT : PET_UI_ACTION_START_TALK;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_OPEN_MENU : key == PET_KEY_NEXT
                ? PET_UI_ACTION_QUICK_SETTINGS : PET_UI_ACTION_MUTE_TOGGLE;
        case PET_KEY_EVENT_COMBO_CONFIRM_NEXT: return PET_UI_ACTION_REBOOT_PROMPT;
        case PET_KEY_EVENT_COMBO_NEXT_VOICE: return PET_UI_ACTION_SCREEN_TOGGLE;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_MENU:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_CONFIRM : key == PET_KEY_NEXT
                ? PET_UI_ACTION_NEXT : PET_UI_ACTION_MENU_CATEGORY_INTERACT;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_CONFIRM : key == PET_KEY_NEXT
                ? PET_UI_ACTION_BACK : PET_UI_ACTION_QUIT_MENU;
        case PET_KEY_EVENT_COMBO_NEXT_VOICE: return PET_UI_ACTION_MENU_CATEGORY_SETTINGS;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_MESSAGE:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_CONFIRM : key == PET_KEY_NEXT
                ? PET_UI_ACTION_PAGE_NEXT : PET_UI_ACTION_MESSAGE_READ;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_MESSAGE_DETAIL : key == PET_KEY_NEXT
                ? PET_UI_ACTION_PAGE_PREV : PET_UI_ACTION_MESSAGE_CLEAR;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_AI_TALK:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_END_TALK : key == PET_KEY_VOICE
                ? PET_UI_ACTION_INTERRUPT_TTS : PET_UI_ACTION_NONE;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_VOICE ? PET_UI_ACTION_MUTE_TOGGLE : PET_UI_ACTION_NONE;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_TTS_PLAYBACK:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_SKIP : key == PET_KEY_NEXT
                ? PET_UI_ACTION_VOLUME_UP : PET_UI_ACTION_REPLAY;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_NEXT ? PET_UI_ACTION_VOLUME_DOWN : key == PET_KEY_VOICE
                ? PET_UI_ACTION_MUTE_TOGGLE : PET_UI_ACTION_NONE;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_SLEEP:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_CONFIRM ? PET_UI_ACTION_WAKE : key == PET_KEY_NEXT
                ? PET_UI_ACTION_PAGE_NEXT : PET_UI_ACTION_SILENT_TALK;
        case PET_KEY_EVENT_LONG_PRESS:
            return key == PET_KEY_VOICE ? PET_UI_ACTION_SCREEN_OFF : PET_UI_ACTION_NONE;
        default: return PET_UI_ACTION_NONE;
        }
    case PET_UI_SCENE_SCREEN_OFF:
        switch (kind) {
        case PET_KEY_EVENT_SHORT_PRESS:
            return key == PET_KEY_VOICE ? PET_UI_ACTION_WAKE_TALK : PET_UI_ACTION_WAKE;
        case PET_KEY_EVENT_COMBO_CONFIRM_NEXT: return PET_UI_ACTION_REBOOT_PROMPT;
        case PET_KEY_EVENT_COMBO_NEXT_VOICE: return PET_UI_ACTION_SCREEN_TOGGLE;
        default: return PET_UI_ACTION_NONE;
        }
    }
    return PET_UI_ACTION_NONE;
}
