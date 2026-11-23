#include "ui_lvgl/api.h"
#include "sdkconfig.h"
#if CONFIG_PET_SETUP_PORTAL_ENABLED
#include "svc_setup/api.h"
#endif

#if CONFIG_PET_MODULE_UI_ENABLED
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pet/presentation/message_presenter.h"
#include "svc_message_hub/api.h"
#include "svc_config/api.h"
#include "message_view.h"
#include "ui_owner.h"
#include "svc_focus/api.h"
#include "esp_app_desc.h"
#if CONFIG_PET_INPUT_TRIPLE
#include "key_actions.h"
#endif
#if CONFIG_PET_EXT_LIGHT_SENSOR
#include "pet/hal/ambient_light.h"
#endif
#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/control_service.h"
#include "control_confirmation.h"
#include "control_view.h"
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#include "climate_view.h"
#include "svc_climate/api.h"
#endif
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED && !CONFIG_PET_AUDIO_DIAGNOSTIC_ON_BOOT
#define PET_UI_AI 1
#include "ai_view.h"
#include "svc_ai_agent/api.h"
#else
#define PET_UI_AI 0
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
#include "growth_input.h"
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "growth_view.h"
#include "svc_growth/api.h"
#include "freertos/queue.h"
#endif

#ifndef CONFIG_PET_UI_DND
#define CONFIG_PET_UI_DND 0
#endif
#ifndef CONFIG_PET_MODULE_GROWTH_ENABLED
#define CONFIG_PET_MODULE_GROWTH_ENABLED 0
#endif

extern const uint8_t font16_start[] asm("_binary_pet_noto_sc_16_bin_start");
extern const uint8_t font16_end[] asm("_binary_pet_noto_sc_16_bin_end");
extern const uint8_t font24_start[] asm("_binary_pet_noto_sc_24_bin_start");
extern const uint8_t font24_end[] asm("_binary_pet_noto_sc_24_bin_end");

static const char *TAG = "ui";
typedef struct {
    const pet_display_t *panel;
    const pet_button_input_t *button;
    pet_ui_clock_t clock;
    lv_display_t *display;
    lv_font_t *font16;
    lv_font_t *font24;
    void *draw_buffer;
    pet_message_view_ui_t view;
    lv_obj_t *privacy;
#if CONFIG_PET_EXT_DEVICE_CONTROL
    pet_control_view_ui_t control_view;
    pet_control_confirmation_t control_input;
    uint64_t control_rendered_id;
    bool control_was_visible;
#endif
    bool dnd, home_rendered, voice_feedback, display_healthy;
    pet_config_snapshot_t settings;
    uint32_t settings_token;
    uint8_t unsaved_privacy; /* bit 0: mute, bit 1: DND; retain local intent after a failed save. */
    bool settings_valid;
    uint32_t home_updated_ms, feedback_until_ms;
    const char *feedback;
    uint32_t last_activity_ms; /* 输入/语音/消息事件驱动；驱动息屏超时与待机亮度。 */
    bool screen_off;
#if CONFIG_PET_EXT_LIGHT_SENSOR
    pet_ambient_filter_t ambient_filter;
    uint32_t ambient_sampled_ms;
    uint16_t ambient_raw;
    bool ambient_sampled, ambient_valid;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    pet_growth_input_t growth_input;
    pet_navigation_filter_t navigation_filter;
    pet_navigation_sample_t navigation_sample;
    bool navigation_available;
    char text[48]; /* ABOUT/设置调整的反馈文本；单 UI 线程独占。 */
#endif
#if CONFIG_PET_INPUT_TRIPLE
    pet_key_tracker_t key_tracker;
    bool keys_available;
#endif
#if PET_UI_AI
    pet_ai_view_ui_t ai_view;
    pet_ai_agent_snapshot_t ai_snapshot;
    bool ai_available, policy_attached, muted, voice_panel, dismiss_pending, reset_gesture;
    bool interrupt_latch; /* 按键打断 TTS：播放期间保持 interrupted，播完自动解除。 */
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    pet_growth_view_ui_t growth_view;
    pet_growth_view_t growth_snapshot;
    QueueHandle_t growth_requests, growth_results;
    TaskHandle_t growth_worker;
    bool growth_valid, growth_pending;
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    pet_climate_view_ui_t climate_view;
    bool climate_panel;
    uint32_t retry_at,climate_pressed_ms;
    bool climate_was_pressed;
#endif
    pet_message_presenter_t presenter;
    pet_message_view_t snapshot[PET_MESSAGE_QUEUE_CAPACITY];
    uint32_t frame_started_ms;
    unsigned strips;
    bool frame_failed;
    bool displayed_pending;
    uint32_t last_ack_ms;
} ui_context_t;
static ui_context_t *s_ui;
static portMUX_TYPE metrics_lock=portMUX_INITIALIZER_UNLOCKED;
static pet_ui_metrics_t metrics;
void pet_ui_metrics(pet_ui_metrics_t *out)
{if (out) {portENTER_CRITICAL(&metrics_lock);*out=metrics;portEXIT_CRITICAL(&metrics_lock);}}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000ULL); }

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    ui_context_t *ui = lv_display_get_user_data(display);
    const uint32_t elapsed = now_ms() - ui->frame_started_ms;
    const size_t count = (size_t)(area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    if (ui->frame_failed || elapsed >= 100) {
        ui->frame_failed = true;
    } else {
        lv_draw_sw_rgb565_swap(pixels, count);
        ui->frame_failed = ui->panel->write(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels,
                                            100 - elapsed) != ESP_OK;
        ++ui->strips;
    }
    lv_display_flush_ready(display);
}

#define PET_UI_IDLE_DIM_MS 30000u /* 静置该时长后降到待机亮度（息屏超时之外的两段亮度）。 */

/* 任一交互事件点亮息屏并重置静置计时；幂等，可在循环内多处调用。 */
static void screen_activity(ui_context_t *ui)
{
    ui->last_activity_ms = now_ms();
    if (ui->screen_off) {
        ui->screen_off = false;
        ui->home_rendered = false; /* 唤醒后强制整帧重绘。 */
    }
}

static uint8_t brightness(ui_context_t *ui)
{
    const uint8_t requested = ui->settings.config.brightness;
    uint8_t applied = requested;
#if CONFIG_PET_EXT_LIGHT_SENSOR
    const uint32_t now = now_ms();
    if (requested && ui->panel->ambient_read &&
        (!ui->ambient_sampled || now - ui->ambient_sampled_ms >= 500)) {
        const bool was_valid = ui->ambient_valid;
        ui->ambient_valid = ui->panel->ambient_read(&ui->ambient_raw) == ESP_OK;
        if (!ui->ambient_valid && (!ui->ambient_sampled || was_valid))
            ESP_LOGW(TAG, "ambient light unavailable; restoring manual brightness");
        ui->ambient_sampled = true;
        ui->ambient_sampled_ms = now;
    }
    applied = pet_ambient_brightness(&ui->ambient_filter, ui->ambient_raw,
        ui->ambient_valid, now, requested);
#else
    (void)requested;
#endif
    /* 两段亮度：静置超过 30s 后降至待机亮度（不把画面调亮过工作亮度）。 */
    if (!ui->screen_off && now_ms() - ui->last_activity_ms >= PET_UI_IDLE_DIM_MS) {
        const uint8_t idle = ui->settings.config.screen.idle_brightness;
        if (idle < applied) applied = idle;
    }
    return applied;
}

static bool refresh(ui_context_t *ui)
{
    if (ui->screen_off) { /* 息屏：跳过绘制只保持背光关闭；唤醒由 screen_activity 触发整帧重绘。 */
        ui->panel->backlight(0);
        return true;
    }
    const uint8_t applied_brightness = brightness(ui);
    ui->frame_failed = false;
    ui->strips = 0;
    ui->frame_started_ms = now_ms();
    lv_refr_now(ui->display);
    uint32_t elapsed=now_ms()-ui->frame_started_ms;
    portENTER_CRITICAL(&metrics_lock);++metrics.frames;metrics.last_frame_ms=elapsed;
    if (elapsed>metrics.max_frame_ms) metrics.max_frame_ms=elapsed;
    portEXIT_CRITICAL(&metrics_lock);
    if (ui->frame_failed || elapsed > 100 ||
        applied_brightness == 0 ||
        ui->panel->backlight(applied_brightness) != ESP_OK) {
        ui->panel->backlight(0);
        ui->display_healthy = false;
        portENTER_CRITICAL(&metrics_lock);++metrics.failed_frames;portEXIT_CRITICAL(&metrics_lock);
        return false;
    }
    ui->display_healthy = true;
    return true;
}

#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI || CONFIG_PET_INPUT_TRIPLE
static void feedback(ui_context_t *ui, const char *text, bool voice)
{
    ui->feedback = text;
    ui->voice_feedback = voice;
    ui->feedback_until_ms = now_ms() + 4000;
}
#endif

static void privacy_badge(ui_context_t *ui)
{
    const char *text = ui->dnd ? "免打扰" : "";
#if PET_UI_AI
    if (ui->ai_available && ui->ai_snapshot.faulted) text = "音频异常 · 请重启";
    else if (ui->ai_available && ui->ai_snapshot.policy_pending) text = "正在停止音频…";
    else if (ui->muted) text = ui->dnd ? "静音 · 免打扰" :
        ui->ai_available ? "语音已静音" : "静音 · 语音未启用";
    else if (ui->ai_available && ui->ai_snapshot.capture_active) text = "麦克风已开启";
    else if (ui->ai_available && ui->ai_snapshot.playback_active && ui->presenter.id[0] != '\0')
        text = "正在播放回复";
    /* AI 主画面已有完整状态；角标只补充被消息/成长画面遮挡的信息。 */
    const bool full_status = ui->ai_snapshot.faulted || (ui->ai_available && ui->ai_snapshot.ready &&
        (ui->ai_snapshot.policy_pending || ui->muted || ui->dnd ||
         ui->ai_snapshot.view.state == PET_AI_LISTENING || ui->ai_snapshot.view.state == PET_AI_SPEAKING ||
         ui->ai_snapshot.view.state == PET_AI_CONVERSATION));
    if (!lv_obj_has_flag(ui->ai_view.root, LV_OBJ_FLAG_HIDDEN) && full_status)
        text = "";
#endif
    if (strcmp(lv_label_get_text(ui->privacy), text) != 0) lv_label_set_text(ui->privacy, text);
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    if (ui->growth_input.menu) lv_obj_add_flag(ui->privacy, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(ui->privacy, LV_OBJ_FLAG_HIDDEN);
#endif
}

#if PET_UI_AI
static bool ai_active(const pet_ai_agent_snapshot_t *snapshot)
{
    return snapshot->view.state == PET_AI_LISTENING || snapshot->view.state == PET_AI_THINKING ||
           snapshot->view.state == PET_AI_SPEAKING || snapshot->view.state == PET_AI_CONVERSATION ||
           snapshot->ptt_requested;
}

static esp_err_t ai_policy(ui_context_t *ui, bool interrupted)
{
    return ui->ai_available ? pet_ai_agent_set_interrupted(interrupted) : ESP_OK;
}

static void ai_snapshot(ui_context_t *ui)
{
    const pet_ai_state_t previous = ui->ai_snapshot.view.state;
    const pet_ai_policy_t previous_policy = ui->ai_snapshot.policy;
    const bool faulted = ui->ai_snapshot.faulted;
    ui->ai_available = pet_ai_agent_snapshot(&ui->ai_snapshot) == ESP_OK;
    if (!ui->ai_available) return;
    if (!ui->policy_attached) {
        /* 保留启动前本地选择；附着以后服务快照是运行期策略的来源。 */
        ui->muted |= ui->ai_snapshot.policy.muted;
        ui->dnd |= ui->ai_snapshot.policy.dnd;
        if (ui->muted) (void)pet_ai_agent_post(PET_AI_MUTE, 0);
        if (ui->dnd) (void)pet_ai_agent_set_dnd(true);
        ui->policy_attached = true;
    } else if (!ui->ai_snapshot.faulted) {
        ui->muted = ui->ai_snapshot.policy.muted;
        ui->dnd = ui->ai_snapshot.policy.dnd;
        const pet_ai_policy_t policy = ui->ai_snapshot.policy;
        ui->reset_gesture = previous_policy.muted != policy.muted || previous_policy.dnd != policy.dnd ||
            previous_policy.interrupted != policy.interrupted;
    }
    if (!ui->dismiss_pending && (ai_active(&ui->ai_snapshot) ||
        (ui->ai_snapshot.view.state == PET_AI_ERROR && previous != PET_AI_ERROR) ||
        (ui->ai_snapshot.faulted && !faulted))) ui->voice_panel = true;
    if (!ai_active(&ui->ai_snapshot)) ui->dismiss_pending = false;
    if (ui->ai_snapshot.view.state == PET_AI_IDLE && previous != PET_AI_IDLE &&
        (ui->ai_snapshot.view.reason == PET_AI_REASON_DRAINED ||
         ui->ai_snapshot.view.reason == PET_AI_REASON_CONV_TIMEOUT))
        ui->voice_panel = false;
}

static void ai_command(ui_context_t *ui, int command)
{
    esp_err_t error = ESP_OK;
    if (command==PET_GROWTH_MENU_AI_CLEAR) {
        error=pet_ai_agent_forget(); feedback(ui,error==ESP_OK?"本地对话已清除":"清除未完成",true);
    } else if (command == PET_GROWTH_MENU_AI_TALK) {
        ui->feedback = NULL;
        ui->dismiss_pending = false;
        ui->voice_panel = true;
        error = pet_ai_agent_ptt(true);
        if (error != ESP_OK) feedback(ui, ui->muted || ui->dnd ? "先关闭静音和免打扰" : "语音暂不可用", true);
    } else if (command == PET_GROWTH_INPUT_AI_RELEASE) {
        error = pet_ai_agent_ptt(false);
    } else if (command == PET_GROWTH_INPUT_AI_CANCEL) {
        ui->feedback = NULL;
        ui->voice_panel = false;
        ui->dismiss_pending = true;
        error = pet_ai_agent_post(PET_AI_CANCEL, 0);
    } else if (command == PET_GROWTH_MENU_AI_MUTE || command == PET_GROWTH_MENU_AI_DND) {
        if (ui->settings_token) { feedback(ui, "正在保存设置", true); return; }
        ui->feedback = NULL;
        ui->dismiss_pending = false;
        if (command == PET_GROWTH_MENU_AI_MUTE) ui->muted = !ui->muted;
        else ui->dnd = !ui->dnd;
        ui->voice_panel = true;
        if (ui->ai_available) error = command == PET_GROWTH_MENU_AI_MUTE ?
            pet_ai_agent_post(ui->muted ? PET_AI_MUTE : PET_AI_UNMUTE, 0) : pet_ai_agent_set_dnd(ui->dnd);
        /* 策略 API 接受的是请求；立刻刷新快照，避免本帧先画出“已停止”。 */
        if (error == ESP_OK && ui->ai_available) (void)pet_ai_agent_snapshot(&ui->ai_snapshot);
        pet_config_t candidate = ui->settings.config;
        if(command==PET_GROWTH_MENU_AI_MUTE) {
            candidate.muted=ui->muted;ui->unsaved_privacy|=1u;
        } else {
            candidate.dnd.manual=ui->dnd;
            if(!ui->dnd)candidate.dnd.enabled=false;
            ui->unsaved_privacy|=2u;
        }
        ui->settings.config = candidate; /* 保存失败也保留本次开机的用户隐私选择。 */
        esp_err_t saved = pet_config_submit(&candidate, ui->settings.revision, &ui->settings_token);
        if (saved != ESP_OK) feedback(ui, "设置未保存，重启后恢复", true);
    }
    if (error != ESP_OK && ui->ai_available && command != PET_GROWTH_MENU_AI_TALK)
        feedback(ui, "语音操作未完成", true);
}
#endif

static void settings_tick(ui_context_t *ui, uint32_t epoch)
{
    if (ui->settings_token) {
        esp_err_t result = ESP_FAIL;
        if (pet_config_result(ui->settings_token, &result) != ESP_ERR_TIMEOUT) {
            ui->settings_token = 0;
            if(result==ESP_OK)ui->unsaved_privacy=0;
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
            if (result != ESP_OK) feedback(ui, "设置未保存，重启后恢复", true);
#endif
        }
    }
    pet_config_snapshot_t snapshot;
    if (!ui->settings_token && pet_config_snapshot(&snapshot) == ESP_OK && snapshot.writable &&
        (!ui->settings_valid || snapshot.revision != ui->settings.revision)) {
        bool muted=ui->settings.config.muted,manual=ui->settings.config.dnd.manual,timed=ui->settings.config.dnd.enabled;
        ui->settings = snapshot; ui->settings_valid = true;
        /* A failed local privacy save must not be undone by a later unrelated revision. */
        if(ui->unsaved_privacy&1u)ui->settings.config.muted=muted;
        if(ui->unsaved_privacy&2u) {ui->settings.config.dnd.manual=manual;ui->settings.config.dnd.enabled=timed;}
#if PET_UI_AI
        ui->muted = ui->settings.config.muted;
        if (ui->ai_available)
            (void)pet_ai_agent_post(ui->muted ? PET_AI_MUTE : PET_AI_UNMUTE, 0);
#endif
    }
    bool dnd = CONFIG_PET_UI_DND || !ui->settings_valid ||
        pet_config_dnd_active(&ui->settings.config, epoch, epoch != 0);
#if PET_UI_AI
    if (ui->settings_token) dnd = ui->dnd; /* 保存期间保留用户刚提交的安全策略。 */
    if (ui->ai_available) {
        (void)pet_ai_agent_set_volume(ui->settings.config.volume);
        (void)pet_ai_agent_set_sfx_volume(ui->settings.config.sfx_volume);
        if (ui->settings_valid) (void)pet_ai_agent_set_dnd(dnd);
    }
#endif
    ui->dnd = dnd;
}

#if CONFIG_PET_MODULE_GROWTH_ENABLED
static void growth_worker(void *arg)
{
    ui_context_t *ui = arg;
    pet_growth_request_t request;
    for (;;) {
        if (xQueueReceive(ui->growth_requests, &request, portMAX_DELAY) == pdTRUE) {
            pet_growth_result_t result = pet_growth_interact(&request);
            xQueueSend(ui->growth_results, &result, portMAX_DELAY);
        }
    }
}
#endif

#if CONFIG_PET_EXT_DEVICE_CONTROL
static bool control_modal(ui_context_t *ui,bool pressed,const pet_message_view_t *messages,size_t count)
{
    pet_control_view_t control={0};
    bool pending=pet_control_service_snapshot(&control)==ESP_OK&&control.awaiting_confirmation;
    bool urgent=false;
    for(size_t i=0;i<count;++i)if(messages[i].priority==PET_MESSAGE_PRIORITY_CRITICAL)urgent=true;
    if(!pending) {
        if(ui->control_was_visible){lv_obj_add_flag(ui->control_view.root,LV_OBJ_FLAG_HIDDEN);ui->home_rendered=false;}
        ui->control_was_visible=false;return false;
    }
    if(!ui->control_was_visible||ui->control_rendered_id!=control.request.id) {
        lv_obj_remove_flag(ui->control_view.root,LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ui->control_view.root);ui->control_rendered_id=control.request.id;
        pet_message_presenter_display_lost(&ui->presenter);
    }
    uint64_t now=(uint64_t)esp_timer_get_time()/1000;
    unsigned remaining=control.expires_ms>now?(unsigned)((control.expires_ms-now+999)/1000):0;
    pet_control_view_ui_show(&ui->control_view,control.request.device,control.request.argument,remaining);
    ui->control_was_visible=true;
    screen_activity(ui); /* 控制确认画面属于交互事件，保持常亮。 */
#if PET_UI_AI
    (void)pet_ai_agent_set_interrupted(true);
    ui->voice_panel=false;
#endif
    bool visible=!urgent&&refresh(ui);
    pet_control_input_result_t decision=pet_control_confirmation_update(&ui->control_input,
        control.request.id,control.confirmation_nonce,pressed,visible,now_ms());
    if(decision!=PET_CONTROL_INPUT_NONE) {
        if(pet_control_service_confirm(control.request.id,control.confirmation_nonce,decision==PET_CONTROL_INPUT_CONFIRM)!=ESP_OK)
            ui->control_input.decided=false;
    }
    if(urgent){lv_obj_add_flag(ui->control_view.root,LV_OBJ_FLAG_HIDDEN);ui->control_was_visible=false;}
    (void)pet_message_presenter_button(&ui->presenter,pressed,now_ms());
    ui->presenter.browse_requested=false;
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    (void)pet_growth_input_update(&ui->growth_input,pressed,true,now_ms());
#endif
    return !urgent;
}
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
static uint8_t clamp_step(int value, int delta, int min)
{
    value += delta;
    return (uint8_t)(value < min ? min : value > 100 ? 100 : value);
}

/* 设置分类的数值调整项：本地立即生效并异步保存；返回 true 表示动作已消费。 */
static bool adjust_setting(ui_context_t *ui, int action)
{
    if (ui->settings_token) { feedback(ui, "正在保存设置", false); return true; }
    pet_config_t candidate = ui->settings.config;
    int value = 0;
    const char *fmt = NULL;
    switch (action) {
    case PET_MENU_VOLUME_UP:
    case PET_MENU_VOLUME_DOWN:
        candidate.volume = clamp_step(candidate.volume, action == PET_MENU_VOLUME_UP ? 5 : -5, 0);
        value = candidate.volume; fmt = "音量 %u"; break;
    case PET_MENU_BRIGHTNESS_UP:
    case PET_MENU_BRIGHTNESS_DOWN:
        /* 亮度 0 会被 refresh 视为显示故障，下限保留 10。 */
        candidate.brightness = clamp_step(candidate.brightness,
            action == PET_MENU_BRIGHTNESS_UP ? 5 : -5, 10);
        value = candidate.brightness; fmt = "亮度 %u"; break;
    case PET_MENU_SFX_UP:
    case PET_MENU_SFX_DOWN:
        candidate.sfx_volume = clamp_step(candidate.sfx_volume,
            action == PET_MENU_SFX_UP ? 5 : -5, 0);
        value = candidate.sfx_volume; fmt = "提示音 %u"; break;
    case PET_MENU_IDLE_BRIGHTNESS:
        candidate.screen.idle_brightness = clamp_step(candidate.screen.idle_brightness, 5, 1);
        value = candidate.screen.idle_brightness; fmt = "待机亮度 %u"; break;
    case PET_MENU_SCREEN_TIMEOUT: {
        static const uint16_t steps[] = {0, 30, 60, 300, 600};
        const unsigned count = sizeof(steps) / sizeof(steps[0]);
        unsigned index = 0;
        for (unsigned i = 0; i < count; ++i)
            if (steps[i] == candidate.screen.timeout_s) index = i;
        candidate.screen.timeout_s = steps[(index + 1) % count];
        value = candidate.screen.timeout_s;
        fmt = value ? "息屏 %u 秒" : "息屏 关闭";
        break;
    }
    default:
        return false;
    }
    ui->settings.config = candidate; /* 保存失败也保留本次开机的用户调整。 */
    (void)pet_config_submit(&candidate, ui->settings.revision, &ui->settings_token);
    snprintf(ui->text, sizeof(ui->text), fmt, (unsigned)value);
    feedback(ui, ui->text, false);
    return true;
}

/* 菜单叶子动作统一分发：BOOT 与三键路径共用。 */
static void handle_selection(ui_context_t *ui, int selected, uint32_t epoch)
{
    if (selected < 0) return;
#if PET_UI_AI
    if (selected == PET_GROWTH_INPUT_AI_RELEASE || selected == PET_GROWTH_INPUT_AI_CANCEL ||
        (selected >= PET_GROWTH_MENU_AI_TALK && selected <= PET_GROWTH_MENU_AI_CLEAR)) {
        ai_command(ui, selected);
        return;
    }
#endif
    if (selected == PET_GROWTH_MENU_RETURN) {
#if PET_UI_AI
        ui->voice_panel = false;
#endif
        return;
    }
#if CONFIG_PET_SETUP_PORTAL_ENABLED
    if (selected == PET_GROWTH_MENU_NETWORK) {
        esp_err_t setup = pet_setup_request();
        feedback(ui, setup == ESP_OK ? "正在打开网络设置…" : "网络设置未完成，请重试", false);
        return;
    }
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    if (selected == PET_GROWTH_MENU_CLIMATE) {
        ui->climate_panel = true;
        return;
    }
#endif
    if (selected >= PET_GROWTH_MENU_FOCUS && selected <= PET_GROWTH_MENU_FOCUS_CANCEL) {
        esp_err_t focus = selected == PET_GROWTH_MENU_FOCUS ? pet_focus_begin(epoch) :
            selected == PET_GROWTH_MENU_FOCUS_PAUSE ? pet_focus_pause() :
            selected == PET_GROWTH_MENU_FOCUS_RESUME ? pet_focus_resume() : pet_focus_stop();
        feedback(ui, focus == ESP_OK ? "专注状态已更新" : "操作未完成，请检查计时和对时", false);
        return;
    }
    if (selected == PET_MENU_MESSAGES_BROWSE) {
        ui->presenter.browse_requested = true;
        return;
    }
    if (adjust_setting(ui, selected)) return;
    if (selected == PET_MENU_ABOUT) {
        snprintf(ui->text, sizeof(ui->text), "版本 %s", esp_app_get_description()->version);
        feedback(ui, ui->text, false);
        return;
    }
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    if (selected >= 0 && selected < PET_GROWTH_ACTION_COUNT) {
        pet_growth_view_t snapshot;
        pet_growth_result_t result = pet_growth_get_snapshot(&snapshot);
        if (result == PET_GROWTH_OK && snapshot.time_ready && snapshot.storage_ready) {
            pet_growth_request_t request = {snapshot.next_request_id,
                (uint64_t)esp_timer_get_time() / 1000, (pet_growth_action_t)selected, snapshot.task_day};
            ui->growth_pending = xQueueSend(ui->growth_requests, &request, 0) == pdTRUE;
            if (!ui->growth_pending) result = PET_GROWTH_NOT_READY;
        } else if (result == PET_GROWTH_OK) {
            result = snapshot.storage_ready ? PET_GROWTH_TIME_UNAVAILABLE : PET_GROWTH_STORAGE_UNCERTAIN;
        }
        if (!ui->growth_pending) feedback(ui, pet_growth_feedback(result), false);
    }
#endif
}
#endif

#if CONFIG_PET_INPUT_TRIPLE
/* 三键场景判定（优先级从高到低）：息屏 > 消息 > AI 对话 > 播报 > 菜单 > 主页。 */
static pet_ui_scene_t key_scene(const ui_context_t *ui)
{
    if (ui->screen_off) return PET_UI_SCENE_SCREEN_OFF;
    if (ui->presenter.id[0] != '\0') return PET_UI_SCENE_MESSAGE;
#if PET_UI_AI
    /* 会话窗口与 LISTENING 同场景：A=结束会话，C 按住续说，C 长按静音。 */
    if (ui->ai_available && ui->ai_snapshot.capture_active &&
        (ui->ai_snapshot.view.state == PET_AI_LISTENING ||
         ui->ai_snapshot.view.state == PET_AI_CONVERSATION)) return PET_UI_SCENE_AI_TALK;
    if (ui->ai_available && ui->ai_snapshot.playback_active) return PET_UI_SCENE_TTS_PLAYBACK;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    if (ui->growth_input.menu) return PET_UI_SCENE_MENU;
#endif
    return PET_UI_SCENE_HOME;
}

static void key_dispatch(ui_context_t *ui, const pet_key_event_t *event, uint32_t epoch)
{
    const pet_ui_scene_t scene = key_scene(ui);
    const pet_ui_action_t action =
        pet_ui_key_action(scene, (pet_key_event_kind_t)event->kind, (pet_key_id_t)event->key);
    if (action == PET_UI_ACTION_NONE) return;
    screen_activity(ui);
    const uint32_t now = now_ms();
    switch (action) {
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    case PET_UI_ACTION_OPEN_MENU:
        pet_growth_input_open_menu(&ui->growth_input, now);
        break;
    case PET_UI_ACTION_CONFIRM:
        if (scene == PET_UI_SCENE_MENU)
            handle_selection(ui, pet_growth_input_activate(&ui->growth_input, now), epoch);
        /* 消息场景的确认已读复用 BOOT 路径语义，直接回执；主页由循环末尾 idle 重绘。 */
        else if (scene == PET_UI_SCENE_MESSAGE && ui->presenter.visible &&
                 pet_message_hub_ack(ui->presenter.id, PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK)
            (void)pet_message_presenter_read_complete(&ui->presenter, ui->presenter.generation);
        break;
    case PET_UI_ACTION_NEXT:
        if (scene == PET_UI_SCENE_MENU) pet_growth_input_move(&ui->growth_input, false, now);
        break;
    case PET_UI_ACTION_BACK:
    case PET_UI_ACTION_QUIT_MENU: {
        const bool quit = action == PET_UI_ACTION_QUIT_MENU;
        do {
            const int cancelled =
                pet_growth_input_navigate(&ui->growth_input, PET_NAV_LEFT, false, now);
#if PET_UI_AI
            if (cancelled == PET_GROWTH_INPUT_AI_CANCEL) ai_command(ui, cancelled);
#endif
        } while (quit && ui->growth_input.menu);
        break;
    }
    case PET_UI_ACTION_MENU_CATEGORY_INTERACT:
    case PET_UI_ACTION_MENU_CATEGORY_SETTINGS:
        if (ui->growth_input.menu) {
            ui->growth_input.page = action == PET_UI_ACTION_MENU_CATEGORY_INTERACT ?
                (uint8_t)PET_MENU_INTERACT : (uint8_t)PET_MENU_SETTINGS;
            ui->growth_input.selection = 0;
            ui->growth_input.last_menu_ms = now;
        }
        break;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    case PET_UI_ACTION_QUICK_SETTINGS:
        if (!ui->growth_input.menu) { /* 待机长按 B 直达设置分类 */
            pet_growth_input_open_menu(&ui->growth_input, now);
            ui->growth_input.page = (uint8_t)PET_MENU_SETTINGS;
            ui->growth_input.selection = 0;
            ui->growth_input.last_menu_ms = now;
        }
        break;
#endif
    case PET_UI_ACTION_PAGE_NEXT:
        ui->presenter.browse_requested = true; /* 主页翻页与消息下一条复用消息浏览 */
        break;
#if PET_UI_AI
    case PET_UI_ACTION_START_TALK:
    case PET_UI_ACTION_WAKE_TALK:
        ai_command(ui, PET_GROWTH_MENU_AI_TALK);
        break;
    case PET_UI_ACTION_END_TALK:
        ai_command(ui, PET_GROWTH_INPUT_AI_CANCEL);
        break;
    case PET_UI_ACTION_MUTE_TOGGLE:
        ai_command(ui, PET_GROWTH_MENU_AI_MUTE);
        break;
    case PET_UI_ACTION_INTERRUPT_TTS:
    case PET_UI_ACTION_SKIP:
        ui->interrupt_latch = true; /* 播放期间保持打断；停播后 ai_policy 自动解除 */
        break;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    case PET_UI_ACTION_VOLUME_UP:
        (void)adjust_setting(ui, PET_MENU_VOLUME_UP);
        break;
    case PET_UI_ACTION_VOLUME_DOWN:
        (void)adjust_setting(ui, PET_MENU_VOLUME_DOWN);
        break;
#endif
    case PET_UI_ACTION_SCREEN_OFF:
        ui->screen_off = true;
        break;
    case PET_UI_ACTION_SCREEN_TOGGLE:
        if (!ui->screen_off) ui->screen_off = true; /* 息屏中按键已由 screen_activity 唤醒 */
        break;
    case PET_UI_ACTION_REBOOT_PROMPT:
        feedback(ui, "重启请使用维护模式", false);
        break;
    case PET_UI_ACTION_MESSAGE_READ:
        feedback(ui, "语音播报开发中", false);
        break;
    default:
        break;
    }
}
#endif

static void idle(ui_context_t *ui, uint8_t count)
{
    lv_obj_add_flag(ui->view.root, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    if (ui->climate_panel) {
#if PET_UI_AI
        lv_obj_add_flag(ui->ai_view.root,LV_OBJ_FLAG_HIDDEN);
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        lv_obj_add_flag(ui->growth_view.root,LV_OBJ_FLAG_HIDDEN);
#endif
        pet_climate_snapshot_t snapshot;pet_climate_chart_t chart;
        bool ready=pet_climate_get_snapshot(&snapshot)==ESP_OK;
        bool points=pet_climate_get_chart(&chart)==ESP_OK;
        lv_obj_remove_flag(ui->climate_view.root,LV_OBJ_FLAG_HIDDEN);
        pet_climate_view_ui_show(&ui->climate_view,ready?&snapshot:NULL,points?&chart:NULL,
            ui->retry_at && now_ms()-ui->retry_at<3000);
        privacy_badge(ui);ui->home_rendered=refresh(ui);ui->home_updated_ms=now_ms();return;
    }
    lv_obj_add_flag(ui->climate_view.root,LV_OBJ_FLAG_HIDDEN);
#endif
#if PET_UI_AI
    const bool voice_menu = ui->growth_input.menu &&
        ui->growth_input.page == PET_MENU_AI;
    const bool voice = voice_menu || (!ui->growth_input.menu && ui->voice_panel) ||
        !CONFIG_PET_MODULE_GROWTH_ENABLED;
    if (voice) {
        lv_obj_remove_flag(ui->ai_view.root, LV_OBJ_FLAG_HIDDEN);
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        lv_obj_add_flag(ui->growth_view.root, LV_OBJ_FLAG_HIDDEN);
        pet_growth_view_ui_tick(&ui->growth_view, now_ms(), false);
#endif
        const pet_ai_agent_snapshot_t *s = &ui->ai_snapshot;
        const pet_ai_screen_t model = {.session = s->view, .available = ui->ai_available&&s->voice_enabled,
            .ready = s->ready, .faulted = s->faulted, .pending = s->policy_pending,
            .muted = ui->muted, .dnd = ui->dnd, .capture_active = s->capture_active,
            .playback_active = s->playback_active, .unread = count,
            .dismissible = ui->voice_panel, .playback_level_pct=s->playback_level_pct,
            .transcript=s->transcript,.reply=s->reply,
            .feedback = ui->voice_feedback && (int32_t)(ui->feedback_until_ms - now_ms()) > 0 ?
                ui->feedback : NULL};
        pet_ai_view_ui_show(&ui->ai_view, &model, &ui->growth_input, now_ms());
        goto render;
    }
    lv_obj_add_flag(ui->ai_view.root, LV_OBJ_FLAG_HIDDEN);
    pet_avatar_tick(&ui->ai_view.avatar, PET_GROWTH_IDLE, now_ms(), false);
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    lv_obj_remove_flag(ui->growth_view.root, LV_OBJ_FLAG_HIDDEN);
    pet_growth_view_t snapshot;
    if (pet_growth_get_snapshot(&snapshot) == PET_GROWTH_OK) {
        ui->growth_snapshot = snapshot;
        ui->growth_valid = true;
    }
    pet_growth_view_ui_show(&ui->growth_view, ui->growth_valid ? &ui->growth_snapshot : NULL,
                            &ui->growth_input, count, false, ui->growth_pending,
                            !ui->voice_feedback && (int32_t)(ui->feedback_until_ms - now_ms()) > 0 ?
                                ui->feedback : NULL);
    pet_focus_t focus;
    if (pet_focus_snapshot(&focus)==ESP_OK && (focus.state==PET_FOCUS_RUNNING||focus.state==PET_FOCUS_PAUSED))
        lv_label_set_text_fmt(ui->growth_view.action,"%s %02u:%02u",focus.state==PET_FOCUS_PAUSED?"已暂停":"专注中",
            (unsigned)(focus.remaining_ms/60000),(unsigned)(focus.remaining_ms/1000%60));
    pet_growth_view_ui_tick(&ui->growth_view, now_ms(), true);
#else
    lv_obj_remove_flag(ui->view.root, LV_OBJ_FLAG_HIDDEN);
    pet_message_view_ui_idle(&ui->view, count, false); /* DND 由跨画面角标呈现。 */
#endif
#if PET_UI_AI
render:
#endif
    privacy_badge(ui);
    ui->home_rendered = refresh(ui);
    ui->home_updated_ms = now_ms();
}

static void ui_task(void *arg)
{
    ui_context_t *ui = arg;
    const esp_err_t init_err = ui->panel->init();
    if (init_err != ESP_OK) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(init_err));
        goto fail;
    }
    if (ui->button->init() != ESP_OK) {
        ESP_LOGE(TAG, "BOOT input init failed");
        goto fail;
    }
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    if (ui->button->navigation_init && ui->button->navigation_read) {
        ui->navigation_available = ui->button->navigation_init() == ESP_OK;
        if (!ui->navigation_available) ESP_LOGW(TAG, "joystick unavailable; using BOOT");
    }
#endif
#if CONFIG_PET_INPUT_TRIPLE
    ui->keys_available = ui->button->keys_init != NULL && ui->button->keys_read != NULL &&
        ui->button->keys_init() == ESP_OK;
    if (!ui->keys_available)
        ESP_LOGW(TAG, "triple keys unavailable; BOOT-only input");
    else
        pet_key_tracker_init(&ui->key_tracker);
#endif
    lv_init();
    lv_tick_set_cb(now_ms);
    ui->font16 = lv_binfont_create_from_buffer((void *)font16_start, font16_end - font16_start);
    ui->font24 = lv_binfont_create_from_buffer((void *)font24_start, font24_end - font24_start);
    if (ui->font16 == NULL || ui->font24 == NULL) {
        ESP_LOGE(TAG, "font allocation failed");
        goto fail;
    }
    ui->draw_buffer = heap_caps_malloc(ui->panel->width * 20 * sizeof(uint16_t),
                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ui->display = lv_display_create(ui->panel->width, ui->panel->height);
    if (ui->draw_buffer == NULL || ui->display == NULL)
        goto fail;
    lv_display_set_user_data(ui->display, ui);
    lv_display_set_color_format(ui->display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(ui->display, ui->draw_buffer, NULL,
                           ui->panel->width * 20 * sizeof(uint16_t),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(ui->display, flush);
    /* 由此任务显式刷新，防止自动刷新把其他帧的完成信号当成本消息的 displayed。 */
    lv_timer_pause(lv_display_get_refr_timer(ui->display));
    lv_obj_t *message_root = lv_obj_create(lv_display_get_screen_active(ui->display));
    lv_obj_set_size(message_root, 240, 320);
    if (!pet_message_view_ui_init(&ui->view, message_root, ui->font16,
                                  ui->font24))
        goto fail;
#if PET_UI_AI
    lv_obj_t *ai_root = lv_obj_create(lv_display_get_screen_active(ui->display));
    pet_ai_view_ui_init(&ui->ai_view, ai_root, ui->font16, ui->font24);
    ui->growth_input.voice_enabled = true;
    ui->growth_input.voice_only = !CONFIG_PET_MODULE_GROWTH_ENABLED;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    lv_obj_t *growth_root = lv_obj_create(lv_display_get_screen_active(ui->display));
    lv_obj_set_size(growth_root, 240, 320);
    pet_growth_view_ui_init(&ui->growth_view, growth_root, ui->font16, ui->font24);
    ui->growth_requests = xQueueCreate(1, sizeof(pet_growth_request_t));
    ui->growth_results = xQueueCreate(1, sizeof(pet_growth_result_t));
    if (!ui->growth_requests || !ui->growth_results ||
        xTaskCreate(growth_worker, "pet_interact", 4096, ui, 2, &ui->growth_worker) != pdPASS)
        goto fail;
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
    ui->growth_input.climate_enabled = true;
#endif
    lv_obj_t *climate_root=lv_obj_create(lv_display_get_screen_active(ui->display));
    pet_climate_view_ui_init(&ui->climate_view,climate_root,ui->font16,ui->font24);
    lv_obj_add_flag(climate_root,LV_OBJ_FLAG_HIDDEN);
#endif
#if CONFIG_PET_SETUP_PORTAL_ENABLED && (CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI)
    ui->growth_input.network_enabled = true;
#endif
    ui->privacy = lv_label_create(lv_display_get_screen_active(ui->display));
#if CONFIG_PET_EXT_DEVICE_CONTROL
    pet_control_view_ui_init(&ui->control_view,lv_obj_create(lv_display_get_screen_active(ui->display)),ui->font16,ui->font24);
    lv_obj_add_flag(ui->control_view.root,LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_set_pos(ui->privacy, 14, 272);
    lv_obj_set_width(ui->privacy, 208);
    lv_obj_set_style_text_font(ui->privacy, ui->font16, 0);
    lv_obj_set_style_text_color(ui->privacy, lv_color_hex(0xE5B461), 0);
    pet_message_presenter_init(&ui->presenter);
    pet_message_presenter_button(&ui->presenter, ui->button->pressed(), now_ms());
    uint8_t idle_count = pet_message_hub_depth();
    idle(ui, idle_count);
    ESP_LOGI(TAG, "ST7789 240x320 UI started; BOOT short press confirms visible message");
    for (;;) {
        const uint32_t epoch = ui->clock != NULL ? ui->clock() : 0;
        settings_tick(ui, epoch);
        /* 息屏超时：无输入/语音/消息事件达到 screen.timeoutS 后关背光（0=永不）。 */
        if (!ui->screen_off && ui->settings.config.screen.timeout_s &&
            now_ms() - ui->last_activity_ms >= (uint32_t)ui->settings.config.screen.timeout_s * 1000u)
            ui->screen_off = true;
        pet_message_hub_purge_expired(epoch);
        const size_t count = pet_message_hub_snapshot(ui->snapshot, PET_MESSAGE_QUEUE_CAPACITY);
        for (size_t i = 0; i < count; ++i)
            if (ui->snapshot[i].delivery.event_id[0])
                ui->snapshot[i].duration_ms = pet_message_display_duration_ms(
                    ui->snapshot[i].duration_ms, ui->settings.config.message.duration_ms);
        const bool pressed = ui->button->pressed();
        if (pressed) screen_activity(ui);
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
        const bool navigation_was_valid = ui->navigation_sample.valid;
        ui->navigation_sample = (pet_navigation_sample_t){0};
        if (ui->navigation_available &&
            ui->button->navigation_read(&ui->navigation_sample) != ESP_OK) {
            ui->navigation_sample.valid = false;
            if (navigation_was_valid) ESP_LOGW(TAG, "joystick sample failed; using BOOT until neutral");
        }
        if (ui->navigation_sample.valid && ui->navigation_sample.pressed) screen_activity(ui);
        const bool navigation_lost = navigation_was_valid && !ui->navigation_sample.valid;
#endif
#if CONFIG_PET_EXT_DEVICE_CONTROL
        if(control_modal(ui,pressed,ui->snapshot,count)) {
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
            (void)pet_navigation_update(&ui->navigation_filter, &ui->navigation_sample, true, now_ms());
#endif
            vTaskDelay(pdMS_TO_TICKS(20));continue;
        }
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        bool climate_input=ui->climate_panel;
        if (ui->climate_panel && pressed!=ui->climate_was_pressed) {
            if (pressed) ui->climate_pressed_ms=now_ms();
            else if (now_ms()-ui->climate_pressed_ms>=800) {
                if (pet_climate_retry()==ESP_OK) ui->retry_at=now_ms();
            } else if (now_ms()-ui->climate_pressed_ms>=30) ui->climate_panel=false;
        }
        ui->climate_was_pressed=pressed;
#if PET_UI_AI
        if (ui->ai_available&&ai_active(&ui->ai_snapshot)) ui->climate_panel=false;
#endif
#endif
#if PET_UI_AI
        ai_snapshot(ui);
        if (ui->ai_available && ai_active(&ui->ai_snapshot)) screen_activity(ui);
        ui->growth_input.voice_active = ui->voice_panel;
        ui->growth_input.voice_ready = ui->ai_available && ui->ai_snapshot.voice_enabled && ui->ai_snapshot.ready && !ui->ai_snapshot.faulted;
        const bool voice_focused = ui->growth_input.holding ||
            (ui->ai_available && ai_active(&ui->ai_snapshot));
#else
        const bool voice_focused = false;
#endif
#if CONFIG_PET_INPUT_TRIPLE
        if (ui->keys_available) {
            bool key_pressed[PET_KEY_COUNT] = {false};
            ui->button->keys_read(key_pressed);
            pet_key_event_t key_events[PET_KEY_COUNT];
            const unsigned key_count = pet_key_tracker_update(&ui->key_tracker,
                key_pressed, now_ms(), key_events, PET_KEY_COUNT);
            for (unsigned i = 0; i < key_count; ++i)
                key_dispatch(ui, &key_events[i], epoch);
        }
#endif
        pet_message_screen_action_t input =
            pet_message_presenter_button(&ui->presenter, pressed, now_ms());
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
        const bool was_menu = ui->growth_input.menu;
        const uint8_t was_selection = ui->growth_input.selection;
        const uint8_t was_page = ui->growth_input.page;
        bool interaction_pending = false;
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        interaction_pending = ui->growth_pending;
#endif
        /* 菜单消费短按，仍向消息状态机输入真实电平，保留跨消息按键代次保护。 */
        bool owns_idle_input = voice_focused || ui->growth_input.press_voice;
#if PET_UI_AI
        owns_idle_input |= ui->voice_panel;
#endif
        if (was_menu || interaction_pending || (ui->presenter.id[0] == '\0' && owns_idle_input)) {
            ui->presenter.browse_requested = false;
            input.kind = PET_MESSAGE_SCREEN_NONE;
        }
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        if (climate_input) {
            ui->presenter.browse_requested=false;input.kind=PET_MESSAGE_SCREEN_NONE;
        }
#endif
        bool defer_popups = voice_focused;
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
        defer_popups |= was_menu || ui->growth_input.holding;
#endif
        pet_message_screen_action_t action = pet_message_presenter_tick_focused(
            &ui->presenter, ui->snapshot, count, now_ms(), epoch, ui->dnd, ui->settings.config.dnd.allow_critical,
            defer_popups);
#if PET_UI_AI
        bool critical = false;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(ui->presenter.id, ui->snapshot[i].id) == 0 &&
                ui->snapshot[i].priority == PET_MESSAGE_PRIORITY_CRITICAL) critical = true;
        }
        /* 按键打断：播放期间保持 interrupted，停播后自动解除。 */
        const bool manual_interrupt = ui->interrupt_latch && ui->ai_snapshot.playback_active;
        ai_policy(ui, critical || !ui->display_healthy || manual_interrupt);
        if (!manual_interrupt) ui->interrupt_latch = false;
        if (critical) ui->voice_panel = false;
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
        bool voice_inhibited = false;
#if PET_UI_AI
        voice_inhibited = ui->growth_input.holding && (!ui->ai_available || !ui->ai_snapshot.ready ||
            ui->ai_snapshot.faulted || ui->muted || ui->dnd);
        voice_inhibited |= ui->reset_gesture;
        ui->reset_gesture = false;
#endif
        const bool navigation_blocked = ui->presenter.id[0] != '\0' || interaction_pending ||
            !ui->home_rendered || voice_inhibited || navigation_lost;
        const pet_navigation_t direction = pet_navigation_update(&ui->navigation_filter,
            &ui->navigation_sample, navigation_blocked, now_ms());
        ui->growth_input.navigation_enabled = ui->navigation_available && ui->navigation_filter.armed;
        const bool menu_pressed = pressed || (ui->growth_input.navigation_enabled &&
            ui->navigation_sample.valid && ui->navigation_sample.pressed);
        int selected = pet_growth_input_update(&ui->growth_input, menu_pressed,
            navigation_blocked
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
            || climate_input
#endif
            ,
            now_ms());
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        if (climate_input) {
            if (direction == PET_NAV_LEFT) ui->climate_panel = false;
        } else
#endif
        if (selected < 0 && direction != PET_NAV_NONE)
            selected = pet_growth_input_navigate(&ui->growth_input, direction, navigation_blocked, now_ms());
        if (menu_pressed || direction != PET_NAV_NONE || selected >= 0) screen_activity(ui);
#if CONFIG_PET_SETUP_PORTAL_ENABLED
        if(pet_setup_request_failed()) feedback(ui,"保存失败，请重试",false);
#endif
        handle_selection(ui, selected, epoch);
        /* 菜单 30s 无操作自动返回主页。 */
        if (ui->growth_input.menu && pet_growth_input_menu_expired(&ui->growth_input, now_ms())) {
#if PET_UI_AI
            if (was_page == PET_MENU_AI) ui->voice_panel = false;
#endif
            ui->home_rendered = false;
        }
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        pet_growth_result_t completed;
        if (xQueueReceive(ui->growth_results, &completed, 0) == pdTRUE) {
            ui->growth_pending = false;
            feedback(ui, pet_growth_feedback(completed), false);
        }
#endif
        if (action.kind == PET_MESSAGE_SCREEN_SHOW) {
            screen_activity(ui); /* 消息展示点亮屏幕并保持常亮。 */
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
            ui->climate_panel=false;lv_obj_add_flag(ui->climate_view.root,LV_OBJ_FLAG_HIDDEN);
#endif
            ui->home_rendered = false;
#if PET_UI_AI
            lv_obj_add_flag(ui->ai_view.root, LV_OBJ_FLAG_HIDDEN);
            pet_avatar_tick(&ui->ai_view.avatar, PET_GROWTH_IDLE, now_ms(), false);
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
            lv_obj_add_flag(ui->growth_view.root, LV_OBJ_FLAG_HIDDEN);
            pet_growth_view_ui_tick(&ui->growth_view, now_ms(), false);
#endif
            lv_obj_remove_flag(ui->view.root, LV_OBJ_FLAG_HIDDEN);
            int16_t utc_offset_min = 0;
            if (pet_config_utc_offset(&ui->settings.config, &utc_offset_min) != ESP_OK)
                utc_offset_min = 0;
            const pet_message_view_t *shown = &ui->snapshot[action.index];
            const bool alert = !action.plain_text && !ui->presenter.manual &&
                               shown->priority == PET_MESSAGE_PRIORITY_CRITICAL;
            pet_message_view_ui_show(&ui->view, shown, action.plain_text, now_ms(), utc_offset_min,
                                     alert);
            privacy_badge(ui);
            const bool rendered = refresh(ui) && ui->strips > 0;
            if (pet_message_presenter_rendered(&ui->presenter, action.generation, rendered,
                                               now_ms())) {
#if PET_UI_AI
                if (shown->state==PET_MESSAGE_STATE_RECEIVED && !ui->muted && !ui->dnd &&
                    shown->sound!=PET_MESSAGE_SOUND_NONE) (void)pet_ai_agent_notify(shown->sound,
                        shown->priority==PET_MESSAGE_PRIORITY_CRITICAL);
#endif
                ui->displayed_pending =
                    pet_message_hub_ack(action.id, PET_MESSAGE_STATE_DISPLAYED) != PET_MESSAGE_OK;
                ui->last_ack_ms = now_ms();
            }
            if (!rendered)
                ESP_LOGW(TAG, "frame failed: generation=%" PRIu32, action.generation);
            if (ui->view.missing_glyphs > 0)
                ESP_LOGW(TAG, "replaced %u unsupported glyphs", ui->view.missing_glyphs);
        } else if ((action.kind == PET_MESSAGE_SCREEN_HIDE ||
                   (ui->presenter.id[0] == '\0' && idle_count != count)) && !ui->screen_off) {
            idle(ui, count);
            idle_count = count;
        }
        action = input;
        if (ui->presenter.visible && ui->displayed_pending && now_ms() - ui->last_ack_ms >= 1000) {
            ui->displayed_pending =
                pet_message_hub_ack(ui->presenter.id, PET_MESSAGE_STATE_DISPLAYED) !=
                PET_MESSAGE_OK;
            ui->last_ack_ms = now_ms();
        }
        if (action.kind == PET_MESSAGE_SCREEN_READ && ui->presenter.visible &&
            action.generation == ui->presenter.generation) {
            const pet_message_error_t err = pet_message_hub_ack(action.id, PET_MESSAGE_STATE_READ);
            if (err == PET_MESSAGE_OK &&
                pet_message_presenter_read_complete(&ui->presenter, action.generation)) {
                idle_count = pet_message_hub_depth();
                idle(ui, idle_count);
            } else if (err != PET_MESSAGE_OK) {
                pet_message_view_ui_read_failed(&ui->view);
            }
        }
        if (ui->presenter.visible) {
            screen_activity(ui); /* 消息可见期间保持常亮。 */
            privacy_badge(ui);
            pet_message_view_ui_tick(&ui->view, now_ms());
            pet_message_view_ui_scroll(&ui->view, now_ms());
            if (!refresh(ui)) {
                /* 后续滚动失败时撤销“当前可见”，禁止对黑屏发送 read。 */
                pet_message_presenter_display_lost(&ui->presenter);
            }
        }
#if CONFIG_PET_MODULE_GROWTH_ENABLED || PET_UI_AI
        if (ui->presenter.id[0] == '\0' && !ui->screen_off &&
            (now_ms() - ui->home_updated_ms >= (voice_focused ? 100u : 200u) ||
             was_menu != ui->growth_input.menu || was_page != ui->growth_input.page ||
             was_selection != ui->growth_input.selection || selected >= 0))
            idle(ui, (uint8_t)count);
#if CONFIG_PET_MODULE_GROWTH_ENABLED
        else if (!ui->screen_off &&
                 pet_growth_view_ui_tick(&ui->growth_view, now_ms(), ui->presenter.id[0] == '\0'))
            ui->home_rendered = refresh(ui);
#endif
#endif
        vTaskDelay(pdMS_TO_TICKS(10));
    }
fail:
#if PET_UI_AI
    (void)pet_ai_agent_set_interrupted(true);
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    if (ui->growth_worker) vTaskDelete(ui->growth_worker);
    if (ui->growth_requests) vQueueDelete(ui->growth_requests);
    if (ui->growth_results) vQueueDelete(ui->growth_results);
#endif
    ui->panel->backlight(0);
    if (ui->display != NULL)
        lv_display_delete(ui->display);
    lv_binfont_destroy(ui->font16);
    lv_binfont_destroy(ui->font24);
    heap_caps_free(ui->draw_buffer);
    heap_caps_free(ui);
    s_ui = NULL;
    pet_ui_owner_release();
    vTaskDelete(NULL);
}

esp_err_t pet_ui_start(const pet_display_t *display, const pet_button_input_t *button,
                       pet_ui_clock_t clock)
{
    if (s_ui != NULL)
        return ESP_ERR_INVALID_STATE;
    if (display == NULL || button == NULL || display->init == NULL || display->write == NULL ||
        display->backlight == NULL || button->init == NULL || button->pressed == NULL)
        return ESP_ERR_INVALID_ARG;
    if(!pet_ui_owner_claim())return ESP_ERR_INVALID_STATE;
    s_ui = heap_caps_calloc(1, sizeof(*s_ui), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_ui == NULL) {
        pet_ui_owner_release();
        return ESP_ERR_NO_MEM;
    }
    s_ui->panel = display;
    s_ui->button = button;
    s_ui->clock = clock;
    s_ui->dnd = CONFIG_PET_UI_DND;
    pet_config_defaults(&s_ui->settings.config);
    if (pet_config_snapshot(&s_ui->settings) == ESP_OK && s_ui->settings.writable) {
        s_ui->settings_valid = true;
        s_ui->dnd |= pet_config_dnd_active(&s_ui->settings.config, clock ? clock() : 0, clock && clock() != 0);
#if PET_UI_AI
        s_ui->muted = s_ui->settings.config.muted;
#endif
    } else s_ui->dnd = true;
    if (xTaskCreate(ui_task, "pet_ui", 12288, s_ui, 4, NULL) != pdPASS) {
        heap_caps_free(s_ui);
        s_ui = NULL;
        pet_ui_owner_release();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
#endif
