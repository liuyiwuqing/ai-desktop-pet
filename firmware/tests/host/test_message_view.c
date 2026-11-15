#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_view.h"
#include "growth_view.h"
#include "ai_view.h"
#include "climate_view.h"
#include "control_view.h"
#include "recovery_view.h"
#include "setup_view.h"
#include "message_fixture.h"

static uint16_t pixels[240 * 320];
static uint16_t draw_buffer[240 * 20];
static unsigned flush_count;

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    ++flush_count;
    const int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y) {
        memcpy(pixels + y * 240 + area->x1, data + (y - area->y1) * width * 2, width * 2);
    }
    lv_display_flush_ready(display);
}

static lv_font_t *load_font(const char *dir, int size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/pet_noto_sc_%d.bin", dir, size);
    FILE *file = fopen(path, "rb");
    assert(file != NULL);
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    rewind(file);
    void *data = malloc(length);
    assert(data != NULL && fread(data, 1, length, file) == (size_t)length);
    fclose(file);
    lv_font_t *font = lv_binfont_create_from_buffer(data, length);
    free(data);
    assert(font != NULL);
    return font;
}

static void screenshot(const char *dir, const char *name, lv_display_t *display)
{
    lv_refr_now(display);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    fprintf(file, "P6\n240 320\n255\n");
    for (size_t i = 0; i < 240 * 320; ++i) {
        const unsigned char rgb[3] = {
            (unsigned char)((pixels[i] >> 11) * 255 / 31),
            (unsigned char)(((pixels[i] >> 5) & 63) * 255 / 63),
            (unsigned char)((pixels[i] & 31) * 255 / 31),
        };
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer, NULL, sizeof(draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_font_t *small = load_font(argv[1], 16);
    lv_font_t *large = load_font(argv[1], 24);
    pet_message_view_ui_t view;
    lv_obj_t *message_root = lv_obj_create(lv_display_get_screen_active(display));
    lv_obj_set_size(message_root, 240, 320);
    assert(pet_message_view_ui_init(&view, message_root, small, large));
    pet_message_view_ui_idle(&view, 0, false);
    screenshot(argv[2], "message-empty", display);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_preview", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.template_id = PET_MESSAGE_TEMPLATE_COMMENT_CARD;
    strcpy(message.title, "收到一条新评论");
    strcpy(message.body, "这篇 ESP32 "
                         "入门很有帮助！\n请问温湿度传感器如何接线？\n期待下一篇教程。\n\n今天也要"
                         "保持好奇心。\n长消息会自动向下滚动，滚动结束不会自动标为已读。");
    pet_message_view_ui_show(&view, &message, false, 1000);
    assert(view.missing_glyphs == 0);
    assert(strcmp(lv_label_get_text(view.body), message.body) == 0);
    screenshot(argv[2], "message-comment", display);
    assert(lv_obj_get_width(view.root) == 240 && lv_obj_get_height(view.root) == 320);
    for (uint32_t t = 2500; t < 8000; t += 40)
        pet_message_view_ui_scroll(&view, t);
    assert(lv_obj_get_scroll_y(view.body_box) > 0);
    pet_message_view_ui_read_failed(&view);
    pet_message_view_ui_scroll(&view, 8500);
    assert(strcmp(lv_label_get_text(view.footer), "确认失败 · 短按重试") == 0);
    screenshot(argv[2], "message-scrolled", display);
    message.priority = PET_MESSAGE_PRIORITY_CRITICAL;
    strcpy(message.title, "室内温度过高");
    strcpy(message.body, "当前温度 35℃，请注意通风。\n设备仍在监测环境变化。");
    pet_message_view_ui_show(&view, &message, false, 9000);
    screenshot(argv[2], "message-critical", display);
    strcpy(message.body, "中文🚀\xFF\x01尾部");
    pet_message_view_ui_show(&view, &message, true, 10000);
    assert(strcmp(lv_label_get_text(view.body), "中文□□□尾部") == 0);
    assert(view.missing_glyphs == 3);
    memset(message.body, 'x', PET_MESSAGE_BODY_MAX);
    message.body[PET_MESSAGE_BODY_MAX] = 0;
    pet_message_view_ui_show(&view, &message, false, 10000);
    assert(strlen(lv_label_get_text(view.body)) == PET_MESSAGE_BODY_MAX);
    pet_message_view_ui_idle(&view, 3, true);
    assert(strcmp(lv_label_get_text(view.body), "") == 0);
    screenshot(argv[2], "message-unread", display);
    lv_obj_clean(lv_display_get_screen_active(display));
    pet_growth_view_ui_t growth;
    lv_obj_t *growth_root = lv_obj_create(lv_display_get_screen_active(display));
    lv_obj_set_size(growth_root, 240, 320);
    pet_growth_view_ui_init(&growth, growth_root, small, large);
    pet_growth_input_t input = {0};
    pet_growth_view_t stats = {.level=1, .exp=25, .exp_to_next=80, .coins=18,
        .energy=78, .hunger=98, .health=100, .mood=33, .intimacy=13,
        .time_ready=true, .storage_ready=true};
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.stats), "金币 18"));
    assert(pet_growth_view_ui_tick(&growth, 0, true));
    assert(pet_growth_view_ui_tick(&growth, 1000, true));
    screenshot(argv[2], "growth-home", display);
    assert(lv_obj_get_width(growth.root) == 240 && lv_obj_get_height(growth.root) == 320);
    stats.environment = PET_GROWTH_ENV_HOT;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.status), "有点热"));
    assert(pet_growth_view_ui_tick(&growth, 1100, true));
    screenshot(argv[2], "growth-hot", display);
    stats.state = PET_GROWTH_SICK;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.status), "需要照料"));
    assert(pet_growth_view_ui_tick(&growth, 1200, true));
    assert(growth.avatar.pose.state == PET_GROWTH_SICK);
    stats.environment = PET_GROWTH_ENV_NONE;
    stats.state = PET_GROWTH_IDLE;
    input.menu = true;
    stats.feed_cooldown_s = 1800;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(!pet_growth_view_ui_tick(&growth, 2000, true));
    assert(lv_obj_has_flag(growth.avatar.object, LV_OBJ_FLAG_HIDDEN));
    assert(strstr(lv_label_get_text(growth.action), "1800"));
    screenshot(argv[2], "growth-menu", display);
    input.navigation_enabled = true;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.footer), "右确认"));
    screenshot(argv[2], "growth-joystick-menu", display);
    lv_obj_update_layout(growth.root);
    assert(lv_obj_get_y(growth.footer) + lv_obj_get_height(growth.footer) <= 320);
    input.navigation_enabled = false;
    input.selection = PET_GROWTH_CLAIM_PET;
    stats.task_day = 20833;
    stats.task_progress[0] = 1; stats.task_progress[1] = 3; stats.task_claimed = 1;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.action), "可领取"));
    screenshot(argv[2], "growth-tasks", display);
    lv_obj_update_layout(growth.root);
    assert(lv_obj_get_y(growth.stats) + lv_obj_get_height(growth.stats) < lv_obj_get_y(growth.action));
    stats.task_claimed |= 2;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.action), "已领取"));
    input.selection = PET_GROWTH_MENU_ACHIEVEMENTS;
    stats.intimacy = 19; stats.level = 4; stats.achievements = PET_GROWTH_ACHIEVEMENT_CARE;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.stats), "亲密 19/20"));
    assert(strstr(lv_label_get_text(growth.stats), "等级 4/5"));
    assert(strstr(lv_label_get_text(growth.stats), "已达成"));
    assert(strcmp(lv_label_get_text(growth.status), "1/3 项已达成") == 0);
    screenshot(argv[2], "growth-achievements", display);
    lv_obj_update_layout(growth.root);
    assert(lv_obj_get_y(growth.stats) + lv_obj_get_height(growth.stats) < lv_obj_get_y(growth.action));
    assert(!pet_growth_view_ui_tick(&growth, 2100, true));
    stats.achievements = PET_GROWTH_ACHIEVEMENT_ALL;
    stats.intimacy = 0; stats.level = 99;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(!strstr(lv_label_get_text(growth.stats), "亲密 0/20"));
    assert(strcmp(lv_label_get_text(growth.status), "3/3 项已达成") == 0);
    screenshot(argv[2], "growth-achievements-complete", display);
    stats.achievements = 0;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strstr(lv_label_get_text(growth.stats), "喂食/摸头/玩耍一次"));
    assert(strcmp(lv_label_get_text(growth.status), "0/3 项已达成") == 0);
    stats.save_recovered = true;
    pet_growth_view_ui_show(&growth, &stats, &input, 3, false, false, NULL);
    assert(strcmp(lv_label_get_text(growth.status), "存档已回退，请核对数值") == 0);
    stats.save_recovered = false;
    input.menu = false;
    stats.mood = -100;
    stats.level = 99; stats.coins = 999999; stats.exp_to_next = 80832;
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, false,
                            pet_growth_feedback(PET_GROWTH_STORAGE_ERROR));
    screenshot(argv[2], "growth-error", display);
    lv_obj_update_layout(growth.root);
    assert(lv_obj_get_y(growth.stats) + lv_obj_get_height(growth.stats) < lv_obj_get_y(growth.action));
    assert(lv_obj_get_y(growth.action) + lv_obj_get_height(growth.action) < lv_obj_get_y(growth.footer));
    assert(lv_obj_get_y(growth.footer) + lv_obj_get_height(growth.footer) <= 320);
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, true, NULL);
    assert(strcmp(lv_label_get_text(growth.action), "正在保存互动…") == 0);
    stats.time_ready = false;
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, false, NULL);
    assert(strcmp(lv_label_get_text(growth.status), "等待时间同步") == 0);
    assert(pet_growth_view_ui_tick(&growth, 2100, true));
    assert(!lv_obj_has_flag(growth.avatar.object, LV_OBJ_FLAG_HIDDEN));
    screenshot(argv[2], "growth-waiting-time", display);
    input.menu = true;
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, false, NULL);
    assert(!pet_growth_view_ui_tick(&growth, 2140, true));
    assert(lv_obj_has_flag(growth.avatar.object, LV_OBJ_FLAG_HIDDEN));
    input.menu = false;
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, false, NULL);
    assert(pet_growth_view_ui_tick(&growth, 2180, true));
    stats.storage_ready = false;
    pet_growth_view_ui_show(&growth, &stats, &input, 0, false, false, NULL);
    assert(strcmp(lv_label_get_text(growth.status), "存档待恢复，请重启") == 0);
    assert(!pet_growth_view_ui_tick(&growth, 2100, true));
    assert(lv_obj_has_flag(growth.avatar.object, LV_OBJ_FLAG_HIDDEN));
    pet_growth_view_ui_show(&growth, NULL, &input, 2, true, false, NULL);
    assert(strcmp(lv_label_get_text(growth.status), "成长服务暂不可用") == 0);
    assert(!pet_growth_view_ui_tick(&growth, 2200, true));
    screenshot(argv[2], "growth-unavailable", display);
    lv_obj_clean(lv_display_get_screen_active(display));
    pet_ai_view_ui_t ai;
    lv_obj_t *ai_root = lv_obj_create(lv_display_get_screen_active(display));
    pet_ai_view_ui_init(&ai, ai_root, small, large);
    pet_climate_view_ui_t climate;
    lv_obj_t *climate_root=lv_obj_create(lv_display_get_screen_active(display));
    pet_climate_view_ui_init(&climate,climate_root,small,large);
    pet_climate_view_ui_show(&climate,NULL,NULL,false);
    assert(!strcmp(lv_label_get_text(climate.status),"传感器暂不可用"));
    screenshot(argv[2],"climate-empty",display);
    pet_climate_snapshot_t climate_model={.reading={.valid=true,.temperature_c=23,.humidity_pct=52},.status=PET_CLIMATE_STATUS_OK};
    pet_climate_chart_t points={0};
    for (unsigned i=30;i<60;++i) points.points[i]=(pet_climate_chart_point_t){(int16_t)(2000+i*5),(int16_t)(5000+i*4),true};
    pet_climate_view_ui_show(&climate,&climate_model,&points,false);
    assert(strstr(lv_label_get_text(climate.values),"23.0"));
    screenshot(argv[2],"climate-chart",display);
    climate_model.status=PET_CLIMATE_STATUS_FAULT;
    pet_climate_view_ui_show(&climate,&climate_model,&points,false);
    assert(strstr(lv_label_get_text(climate.alert),"重试"));
    screenshot(argv[2],"climate-fault",display);
    lv_obj_add_flag(climate_root,LV_OBJ_FLAG_HIDDEN);
    pet_ai_screen_t unavailable = {0};
    pet_ai_view_ui_show(&ai, &unavailable, &input, 0);
    assert(strcmp(lv_label_get_text(ai.title), "语音未启用") == 0 && lv_bar_get_value(ai.level) == 0);
    unavailable.available = true;
    pet_ai_view_ui_show(&ai, &unavailable, &input, 100);
    assert(strcmp(lv_label_get_text(ai.title), "语音启动中") == 0);
    pet_ai_screen_t model = {.available = true, .ready = true, .capture_active = true,
        .session = {.state = PET_AI_LISTENING, .push_to_talk = true, .level_pct = 72}};
    input = (pet_growth_input_t){.voice_enabled = true, .voice_only = true};
    model.session.state = PET_AI_IDLE;
    pet_ai_view_ui_show(&ai, &model, &input, 800);
    assert(strcmp(lv_label_get_text(ai.title), "等待唤醒") == 0);
    assert(strcmp(lv_label_get_text(ai.status), "请说“小爱同学”") == 0 && lv_bar_get_value(ai.level) == 0);
    screenshot(argv[2], "ai-wake-idle", display);
    input.menu=true;input.selection=PET_GROWTH_MENU_AI_TALK;
    pet_ai_view_ui_show(&ai,&model,&input,900);lv_refr_now(display);
    unsigned unchanged=flush_count;
    pet_ai_view_ui_show(&ai,&model,&input,900);lv_refr_now(display);
    assert(flush_count==unchanged); /* 没有文字/动画变化时不重绘静态菜单。 */
    input.menu=false;
    model.session.state = PET_AI_LISTENING;
    pet_ai_view_ui_show(&ai, &model, &input, 1000);
    assert(strcmp(lv_label_get_text(ai.title), "正在听") == 0);
    assert(lv_bar_get_value(ai.level) == 72);
    screenshot(argv[2], "ai-listening", display);
    model.session.level_pct = 255;
    pet_ai_view_ui_show(&ai, &model, &input, 1100);
    assert(lv_bar_get_value(ai.level) == 100);
    model.session.state = PET_AI_THINKING;
    model.capture_active = false;
    pet_ai_view_ui_show(&ai, &model, &input, 1200);
    assert(strcmp(lv_label_get_text(ai.title), "正在思考") == 0 && lv_bar_get_value(ai.level) == 0);
    screenshot(argv[2], "ai-thinking", display);
    model.session.state = PET_AI_SPEAKING;
    model.playback_active=true; model.playback_level_pct=64; model.reply="你好，我是豆豆";
    pet_ai_view_ui_show(&ai, &model, &input, 1400);
    assert(strcmp(lv_label_get_text(ai.title), "正在说话") == 0);
    assert(lv_bar_get_value(ai.level)==64 && ai.avatar.speaking);
    assert(!strcmp(lv_label_get_text(ai.detail),"你好，我是豆豆"));
    screenshot(argv[2], "ai-speaking", display);
    model.playback_active=false; model.reply=NULL;
    model.session.state = PET_AI_ERROR;
    model.session.reason = PET_AI_REASON_RESPONSE_TIMEOUT;
    pet_ai_view_ui_show(&ai, &model, &input, 1600);
    assert(strstr(lv_label_get_text(ai.status), "超时"));
    screenshot(argv[2], "ai-error", display);
    model.capture_active = true;
    pet_ai_view_ui_show(&ai, &model, &input, 1650);
    assert(strstr(lv_label_get_text(ai.status), "超时") &&
           strstr(lv_label_get_text(ai.status), "麦克风已开启"));
    lv_obj_update_layout(ai.root);
    assert(lv_obj_get_y(ai.status) + lv_obj_get_height(ai.status) <= lv_obj_get_y(ai.avatar.object));
    screenshot(argv[2], "ai-error-listening", display);
    input.menu = true;
    input.selection = PET_GROWTH_MENU_AI_TALK;
    pet_ai_view_ui_show(&ai, &model, &input, 1700);
    assert(strcmp(lv_label_get_text(ai.status), "麦克风已开启") == 0);
    input.menu = false;
    model.capture_active = false;
    model.muted = true;
    model.pending = true;
    pet_ai_view_ui_show(&ai, &model, &input, 1800);
    assert(strstr(lv_label_get_text(ai.status), "正在停止"));
    model.pending = false;
    pet_ai_view_ui_show(&ai, &model, &input, 2000);
    assert(strcmp(lv_label_get_text(ai.title), "语音已静音") == 0);
    assert(strcmp(lv_label_get_text(ai.status), "采集和播放已停止") == 0);
    screenshot(argv[2], "ai-muted", display);
    model.faulted = true;
    pet_ai_view_ui_show(&ai, &model, &input, 2200);
    assert(strstr(lv_label_get_text(ai.status), "重启"));
    assert(!strstr(lv_label_get_text(ai.status), "已停止"));
    screenshot(argv[2], "ai-fault", display);
    input.menu = true;
    input.selection = PET_GROWTH_MENU_AI_DND;
    model.faulted = false;
    model.dnd = true;
    pet_ai_view_ui_show(&ai, &model, &input, 2400);
    assert(strcmp(lv_label_get_text(ai.detail), "关闭免打扰") == 0);
    screenshot(argv[2], "ai-menu", display);
    input.navigation_enabled = true;
    input.selection = PET_GROWTH_MENU_AI_TALK;
    pet_ai_view_ui_show(&ai, &model, &input, 2400);
    assert(strstr(lv_label_get_text(ai.footer), "中键说话"));
    screenshot(argv[2], "ai-joystick-menu", display);
    lv_obj_update_layout(ai.root);
    assert(lv_obj_get_y(ai.detail) + lv_obj_get_height(ai.detail) < lv_obj_get_y(ai.footer));
    assert(lv_obj_get_y(ai.footer) + lv_obj_get_height(ai.footer) <= 320);
    lv_obj_t *control_root=lv_obj_create(lv_display_get_screen_active(display));
    pet_control_view_ui_t control;pet_control_view_ui_init(&control,control_root,small,large);
    pet_control_view_ui_show(&control,"mac","IntelliJ_IDEA",60);
    lv_obj_update_layout(control_root);
    assert(strstr(lv_label_get_text(control.footer),"确认")&&strstr(lv_label_get_text(control.remaining),"60"));
    assert(lv_obj_get_y(control.footer)+lv_obj_get_height(control.footer)<=320);
    assert(lv_obj_get_y(control.action)+lv_obj_get_height(control.action)<lv_obj_get_y(control.remaining));
    screenshot(argv[2],"control-confirmation",display);
    lv_obj_t *recovery_root=lv_obj_create(lv_display_get_screen_active(display));
    pet_recovery_view_ui_t recovery;
    assert(pet_recovery_view_ui_init(&recovery,recovery_root,small,large));
    pet_recovery_view_ui_show(&recovery,PET_RECOVERY_PROMPT_IDLE,true);
    assert(strstr(lv_label_get_text(recovery.action),"长按 BOOT 3 秒"));
    screenshot(argv[2],"recovery-idle",display);
    for(int prompt=PET_RECOVERY_PROMPT_RELEASE;prompt<=PET_RECOVERY_PROMPT_RESTARTING;++prompt) {
        pet_recovery_view_ui_show(&recovery,(pet_recovery_prompt_t)prompt,true);
        lv_obj_update_layout(recovery_root);
        assert(lv_obj_get_height(recovery.action)+lv_obj_get_y(recovery.action)<=304);
        assert(lv_obj_get_height(recovery.detail)+lv_obj_get_y(recovery.detail)<210);
    }
    pet_recovery_view_ui_show(&recovery,PET_RECOVERY_PROMPT_CONFIRM,true);
    assert(strstr(lv_label_get_text(recovery.action),"短按取消"));
    screenshot(argv[2],"recovery-confirm",display);
    pet_recovery_view_ui_show(&recovery,PET_RECOVERY_PROMPT_IDLE,false);
    assert(strstr(lv_label_get_text(recovery.action),"BOOT 不可用"));
    screenshot(argv[2],"recovery-no-button",display);
    lv_obj_add_flag(recovery_root,LV_OBJ_FLAG_HIDDEN);
    pet_setup_view_t setup;
    lv_obj_t *setup_root=lv_obj_create(lv_display_get_screen_active(display));
    pet_setup_view_init(&setup,setup_root,small,large);
    pet_setup_snapshot_t setup_state={.state=PET_SETUP_READY,.remaining_s=900};
    strcpy(setup_state.ap_ssid,"Qitomo-1234");strcpy(setup_state.ap_password,"2345ABCDEFGH");
    pet_setup_view_show(&setup,&setup_state);
    assert(strstr(lv_label_get_text(setup.ssid),"Qitomo-1234"));
    assert(strstr(lv_label_get_text(setup.password),"2345ABCDEFGH"));
    assert(strstr(lv_label_get_text(setup.address),"http://192.168.4.1"));
    screenshot(argv[2],"setup-ready",display);
    setup_state.state=PET_SETUP_WAITING_BIND;strcpy(setup_state.activation_code,"123456");
    pet_setup_view_show(&setup,&setup_state);
    assert(strstr(lv_label_get_text(setup.action),"123456"));
    screenshot(argv[2],"setup-bind",display);
    for(int state=PET_SETUP_STARTING;state<=PET_SETUP_LEAVING;state++) {
        setup_state.state=(pet_setup_state_t)state;pet_setup_view_show(&setup,&setup_state);lv_obj_update_layout(setup_root);
        assert(lv_obj_get_y(setup.state)+lv_obj_get_height(setup.state)<=100);
        assert(lv_obj_get_y(setup.password)+lv_obj_get_height(setup.password)<193);
        assert(lv_obj_get_y(setup.action)+lv_obj_get_height(setup.action)<=320);
    }
    lv_display_delete(display);
    lv_binfont_destroy(small);
    lv_binfont_destroy(large);
    lv_deinit();
    puts("message view: PASS (Chinese, long body, scroll, missing glyphs, max length, idle)");
    return 0;
}
