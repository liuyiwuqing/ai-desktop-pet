#pragma once

#include <stdint.h>
#include "pet/hal/keys.h"

/* 三键业务映射（docs/30 §3.3 矩阵）：纯表驱动，hal 只产出按键事件，
 * 本表按场景翻译成 UI 动作，供 ui_service/菜单状态机执行。 */
typedef enum {
    PET_UI_SCENE_HOME = 0,     /* 主页：表情/状态页 */
    PET_UI_SCENE_MENU,         /* 一级/二级菜单 */
    PET_UI_SCENE_MESSAGE,      /* 消息展示 */
    PET_UI_SCENE_AI_TALK,      /* AI 对话中 */
    PET_UI_SCENE_TTS_PLAYBACK, /* 非 AI 对话的播报中 */
    PET_UI_SCENE_SLEEP,        /* 睡眠 */
    PET_UI_SCENE_SCREEN_OFF    /* 已息屏 */
} pet_ui_scene_t;

typedef enum {
    PET_UI_ACTION_NONE = 0,
    PET_UI_ACTION_OPEN_MENU,
    PET_UI_ACTION_CONFIRM,       /* 确认/进入/已读 */
    PET_UI_ACTION_NEXT,          /* 菜单下一项 */
    PET_UI_ACTION_BACK,          /* 返回上级 */
    PET_UI_ACTION_QUIT_MENU,     /* 退出菜单到待机 */
    PET_UI_ACTION_PAGE_NEXT,     /* 主页切换表情/状态页、消息下一条 */
    PET_UI_ACTION_PAGE_PREV,     /* 消息上一条 */
    PET_UI_ACTION_QUICK_SETTINGS,/* 待机 KEY_B 长按 */
    PET_UI_ACTION_START_TALK,    /* 开始 AI 对话 */
    PET_UI_ACTION_END_TALK,      /* 结束对话回待机 */
    PET_UI_ACTION_INTERRUPT_TTS, /* 手动打断 TTS（barge-in 兜底） */
    PET_UI_ACTION_MUTE_TOGGLE,   /* 所有场景 KEY_C 长按恒定 */
    PET_UI_ACTION_REPLAY,
    PET_UI_ACTION_SKIP,
    PET_UI_ACTION_VOLUME_UP,
    PET_UI_ACTION_VOLUME_DOWN,
    PET_UI_ACTION_MESSAGE_READ,   /* 语音播报本条 */
    PET_UI_ACTION_MESSAGE_DETAIL,
    PET_UI_ACTION_MESSAGE_CLEAR,
    PET_UI_ACTION_MENU_CATEGORY_INTERACT, /* 菜单内直达“互动”分类 */
    PET_UI_ACTION_MENU_CATEGORY_SETTINGS, /* 菜单内直达“设置”分类 */
    PET_UI_ACTION_WAKE,           /* 睡眠唤醒/息屏点亮 */
    PET_UI_ACTION_WAKE_TALK,      /* 息屏点亮并开始对话 */
    PET_UI_ACTION_SILENT_TALK,    /* 睡眠中静音对话 */
    PET_UI_ACTION_SCREEN_OFF,     /* 进入息屏 */
    PET_UI_ACTION_SCREEN_TOGGLE,  /* B+C：息屏开关 */
    PET_UI_ACTION_REBOOT_PROMPT   /* A+B：重启提示 */
} pet_ui_action_t;

pet_ui_action_t pet_ui_key_action(pet_ui_scene_t scene, pet_key_event_kind_t kind, pet_key_id_t key);
