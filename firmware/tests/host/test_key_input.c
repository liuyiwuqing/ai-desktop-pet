#include <assert.h>
#include <stdio.h>
#include "pet/hal/keys.h"
#include "key_actions.h"

/* 每个 feed tick 推进给定 ms；按键 A/B/C 对应 CONFIRM/NEXT/VOICE。 */
static unsigned feed(pet_key_tracker_t *t, uint32_t now, bool a, bool b, bool c,
    pet_key_event_t *out, unsigned capacity)
{
    const bool pressed[PET_KEY_COUNT] = {a, b, c};
    return pet_key_tracker_update(t, pressed, now, out, capacity);
}

static void test_tracker_single_key(void)
{
    pet_key_tracker_t t;
    pet_key_event_t events[3];
    pet_key_tracker_init(&t);

    /* 抖动：30ms 内的电平翻转不产生事件，稳定后按正常沿处理。 */
    assert(feed(&t, 0, true, false, false, events, 3) == 0);
    assert(feed(&t, 10, false, false, false, events, 3) == 0);
    assert(feed(&t, 20, true, false, false, events, 3) == 0);
    assert(feed(&t, 25, false, false, false, events, 3) == 0);
    assert(feed(&t, 40, true, false, false, events, 3) == 0); /* 重新消抖 */
    assert(feed(&t, 300, true, false, false, events, 3) == 0); /* 稳定按下沿 */
    assert(feed(&t, 320, false, false, false, events, 3) == 0);
    assert(feed(&t, 400, false, false, false, events, 3) == 1); /* 短按：稳定释放 */
    assert(events[0].kind == PET_KEY_EVENT_SHORT_PRESS && events[0].key == PET_KEY_CONFIRM);

    /* 长按：按住 >=800ms 产生一次 LONG_PRESS，释放不再产生 SHORT。 */
    assert(feed(&t, 500, true, false, false, events, 3) == 0);
    assert(feed(&t, 900, true, false, false, events, 3) == 0); /* 稳定按下，press_ms=900 */
    assert(feed(&t, 1301, true, false, false, events, 3) == 0); /* 不足 800ms */
    assert(feed(&t, 1701, true, false, false, events, 3) == 1);
    assert(events[0].kind == PET_KEY_EVENT_LONG_PRESS && events[0].key == PET_KEY_CONFIRM);
    assert(feed(&t, 1702, true, false, false, events, 3) == 0); /* 只发一次 */
    assert(feed(&t, 1800, false, false, false, events, 3) == 0);
    assert(feed(&t, 1900, false, false, false, events, 3) == 0);
    puts("key tracker: debounce, short and long press passed");
}

static void test_tracker_combo(void)
{
    pet_key_tracker_t t;
    pet_key_event_t events[3];
    pet_key_tracker_init(&t);

    /* A+B 重叠窗口内：抑制单键事件，两键都稳定释放后产生组合事件。 */
    assert(feed(&t, 0, true, false, false, events, 3) == 0); /* A 消抖 */
    assert(feed(&t, 40, true, false, false, events, 3) == 0); /* A 稳定按下 */
    assert(feed(&t, 80, true, true, false, events, 3) == 0); /* B 消抖（窗口内） */
    assert(feed(&t, 120, true, true, false, events, 3) == 0); /* B 稳定按下 → 组合成立 */
    assert(feed(&t, 900, true, true, false, events, 3) == 0); /* 持续按住：无长按 */
    assert(feed(&t, 920, false, true, false, events, 3) == 0); /* A 释放消抖 */
    assert(feed(&t, 960, false, true, false, events, 3) == 0); /* A 稳定释放：等待 B */
    assert(feed(&t, 980, false, false, false, events, 3) == 0); /* B 释放消抖 */
    assert(feed(&t, 1020, false, false, false, events, 3) == 1); /* B 稳定释放：组合 */
    assert(events[0].kind == PET_KEY_EVENT_COMBO_CONFIRM_NEXT && events[0].key == 0);

    /* B+C：息屏开关组合。 */
    assert(feed(&t, 1100, false, true, false, events, 3) == 0);
    assert(feed(&t, 1140, false, true, false, events, 3) == 0); /* B 稳定按下 */
    assert(feed(&t, 1180, false, true, true, events, 3) == 0); /* C 消抖 */
    assert(feed(&t, 1220, false, true, true, events, 3) == 0); /* C 稳定按下 → 组合 */
    assert(feed(&t, 1300, false, false, false, events, 3) == 0); /* 释放消抖 */
    assert(feed(&t, 1340, false, false, false, events, 3) == 1); /* 两键释放：组合事件 */
    assert(events[0].kind == PET_KEY_EVENT_COMBO_NEXT_VOICE);

    /* 超出组合窗口：A 长按后 B 独立短按（B 按下时 A 已长按，不组合）。 */
    assert(feed(&t, 2000, true, false, false, events, 3) == 0);
    assert(feed(&t, 2040, true, false, false, events, 3) == 0); /* A 稳定按下 */
    assert(feed(&t, 3000, true, false, false, events, 3) == 1); /* A 长按 */
    assert(events[0].kind == PET_KEY_EVENT_LONG_PRESS);
    assert(feed(&t, 4000, true, true, false, events, 3) == 0);
    assert(feed(&t, 4040, true, true, false, events, 3) == 0); /* B 稳定按下：无组合 */
    assert(feed(&t, 4080, false, false, false, events, 3) == 0); /* A/B 释放消抖 */
    assert(feed(&t, 4120, false, false, false, events, 3) == 1); /* A 稳定释放（长按不发短按），B 稳定释放：短按 */
    assert(events[0].kind == PET_KEY_EVENT_SHORT_PRESS && events[0].key == PET_KEY_NEXT);
    assert(feed(&t, 4160, false, false, false, events, 3) == 0);
    puts("key tracker: combo suppression and window passed");
}

static void test_tracker_edges(void)
{
    pet_key_tracker_t t;
    pet_key_event_t events[3];
    pet_key_tracker_init(&t);
    assert(pet_key_tracker_update(NULL, 0, 0, NULL, 0) == 0);
    assert(pet_key_tracker_update(&t, NULL, 0, NULL, 0) == 0);
    pet_key_tracker_init(NULL); /* 空指针安全 */

    /* 容量不足时事件被丢弃，仍返回总数；A+C 同时按下不是已定义组合，各自独立。 */
    assert(feed(&t, 0, true, false, true, events, 0) == 0); /* 消抖中 */
    assert(feed(&t, 200, true, false, true, events, 0) == 0); /* 稳定按下：无已定义组合 */
    assert(feed(&t, 400, false, false, false, events, 0) == 0); /* 释放消抖 */
    assert(feed(&t, 600, false, false, false, events, 0) == 2); /* 两个独立短按 */
    puts("key tracker: null safety, capacity overflow and undefined combo passed");
}

static void test_key_actions(void)
{
    /* 待机。 */
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_OPEN_MENU);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_LONG_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_OPEN_MENU);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_PAGE_NEXT);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_LONG_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_QUICK_SETTINGS);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_START_TALK);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MUTE_TOGGLE);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_COMBO_CONFIRM_NEXT, PET_KEY_CONFIRM) == PET_UI_ACTION_REBOOT_PROMPT);
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_COMBO_NEXT_VOICE, PET_KEY_NEXT) == PET_UI_ACTION_SCREEN_TOGGLE);
    /* 菜单。 */
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_CONFIRM);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_LONG_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_CONFIRM);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_NEXT);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_LONG_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_BACK);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_START_TALK);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MUTE_TOGGLE);
    assert(pet_ui_key_action(PET_UI_SCENE_MENU, PET_KEY_EVENT_COMBO_NEXT_VOICE, PET_KEY_NEXT) == PET_UI_ACTION_MENU_CATEGORY_SETTINGS);
    /* 消息。 */
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_CONFIRM);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_LONG_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_MESSAGE_DETAIL);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_PAGE_NEXT);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_LONG_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_PAGE_PREV);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MESSAGE_READ);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MESSAGE_CLEAR);
    /* AI 对话。 */
    assert(pet_ui_key_action(PET_UI_SCENE_AI_TALK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_END_TALK);
    assert(pet_ui_key_action(PET_UI_SCENE_AI_TALK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_NONE);
    assert(pet_ui_key_action(PET_UI_SCENE_AI_TALK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_INTERRUPT_TTS);
    assert(pet_ui_key_action(PET_UI_SCENE_AI_TALK, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MUTE_TOGGLE);
    /* TTS 播放。 */
    assert(pet_ui_key_action(PET_UI_SCENE_TTS_PLAYBACK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_SKIP);
    assert(pet_ui_key_action(PET_UI_SCENE_TTS_PLAYBACK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_VOLUME_UP);
    assert(pet_ui_key_action(PET_UI_SCENE_TTS_PLAYBACK, PET_KEY_EVENT_LONG_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_VOLUME_DOWN);
    assert(pet_ui_key_action(PET_UI_SCENE_TTS_PLAYBACK, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_REPLAY);
    assert(pet_ui_key_action(PET_UI_SCENE_TTS_PLAYBACK, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_MUTE_TOGGLE);
    /* 睡眠。 */
    assert(pet_ui_key_action(PET_UI_SCENE_SLEEP, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_WAKE);
    assert(pet_ui_key_action(PET_UI_SCENE_SLEEP, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_PAGE_NEXT);
    assert(pet_ui_key_action(PET_UI_SCENE_SLEEP, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_SILENT_TALK);
    assert(pet_ui_key_action(PET_UI_SCENE_SLEEP, PET_KEY_EVENT_LONG_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_SCREEN_OFF);
    /* 息屏。 */
    assert(pet_ui_key_action(PET_UI_SCENE_SCREEN_OFF, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_CONFIRM) == PET_UI_ACTION_WAKE);
    assert(pet_ui_key_action(PET_UI_SCENE_SCREEN_OFF, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_NEXT) == PET_UI_ACTION_WAKE);
    assert(pet_ui_key_action(PET_UI_SCENE_SCREEN_OFF, PET_KEY_EVENT_SHORT_PRESS, PET_KEY_VOICE) == PET_UI_ACTION_WAKE_TALK);
    /* 未知事件一律 NONE。 */
    assert(pet_ui_key_action(PET_UI_SCENE_HOME, PET_KEY_EVENT_NONE, PET_KEY_CONFIRM) == PET_UI_ACTION_NONE);
    assert(pet_ui_key_action(PET_UI_SCENE_MESSAGE, PET_KEY_EVENT_COMBO_CONFIRM_NEXT, PET_KEY_CONFIRM) == PET_UI_ACTION_NONE);
    puts("key actions: scene x key mapping matrix passed");
}

int main(void)
{
    test_tracker_single_key();
    test_tracker_combo();
    test_tracker_edges();
    test_key_actions();
    puts("key input: all tracker and mapping tests passed");
}
