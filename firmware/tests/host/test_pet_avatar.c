#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet_avatar.h"

static uint16_t pixels[240 * 320], baseline[240 * 320];
static uint16_t buffer[240 * 20];
static unsigned flushes;
static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *data)
{
    int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; ++y)
        memcpy(pixels + y * 240 + area->x1, data + (y - area->y1) * width * 2, width * 2);
    ++flushes;
    lv_display_flush_ready(display);
}
static void export_frame(const char *directory, unsigned state, unsigned frame)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/pet-%u-%02u.ppm", directory, state, frame);
    FILE *file = fopen(path, "wb");
    assert(file);
    fprintf(file, "P6\n240 320\n255\n");
    for (size_t i = 0; i < 240 * 320; ++i) {
        unsigned char rgb[] = {(pixels[i] >> 11) * 255 / 31,
            ((pixels[i] >> 5) & 63) * 255 / 63, (pixels[i] & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    fclose(file);
}
static uint32_t hash(void)
{
    uint32_t value = 2166136261u;
    for (size_t i = 0; i < 240 * 320; ++i) value = (value ^ pixels[i]) * 16777619u;
    return value;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    lv_init();
    lv_display_t *display = lv_display_create(240, 320);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_obj_t *root = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(root, lv_color_hex(0x101923), 0);
    pet_avatar_t avatar;
    pet_avatar_init(&avatar, root);
    lv_refr_now(display);
    memcpy(baseline, pixels, sizeof(pixels));
    uint32_t states[8];
    for (unsigned state = 0; state < 8; ++state) {
        pet_avatar_tick(&avatar, state, 0, false);
        bool moved = false;
        uint32_t first = 0;
        for (unsigned frame = 0; frame < 42; ++frame) {
            assert(pet_avatar_tick(&avatar, state, frame * 100, true));
            lv_refr_now(display);
            if (!frame) first = hash();
            else moved |= hash() != first;
            if (frame == 10) states[state] = hash();
            for (int y = 0; y < 320; ++y) for (int x = 0; x < 240; ++x)
                if (x < 14 || x >= 222 || y < 62 || y >= 212)
                    assert(pixels[y * 240 + x] == baseline[y * 240 + x]);
            export_frame(argv[1], state, frame);
        }
        assert(moved); /* 确认真正的帧缓冲有变化，而非只改变内存中的动画参数。 */
    }
    for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = i + 1; j < 8; ++j) assert(states[i] != states[j]);
    uint32_t environments[5];
    for (unsigned environment = 0; environment <= PET_GROWTH_ENV_HUMID; ++environment) {
        pet_avatar_set_environment(&avatar, (pet_growth_environment_t)environment);
        pet_avatar_tick(&avatar, PET_GROWTH_IDLE, 0, false);
        bool moved = false;
        uint32_t first = 0;
        for (unsigned frame = 0; frame < 42; ++frame) {
            assert(pet_avatar_tick(&avatar, PET_GROWTH_IDLE, frame * 100, true));
            lv_refr_now(display);
            if (!frame) first = hash();
            else moved |= hash() != first;
            if (frame == 10) environments[environment] = hash();
            for (int y = 0; y < 320; ++y) for (int x = 0; x < 240; ++x)
                if (x < 14 || x >= 222 || y < 62 || y >= 212)
                    assert(pixels[y * 240 + x] == baseline[y * 240 + x]);
            if (environment) export_frame(argv[1], environment + 7, frame);
        }
        assert(moved);
    }
    for (unsigned i = 0; i < 5; ++i)
        for (unsigned j = i + 1; j < 5; ++j) assert(environments[i] != environments[j]);
    const pet_growth_state_t priority[] = {PET_GROWTH_SICK, PET_GROWTH_SLEEPING};
    for (unsigned i = 0; i < sizeof(priority) / sizeof(priority[0]); ++i) {
        pet_avatar_set_environment(&avatar, PET_GROWTH_ENV_NONE);
        pet_avatar_tick(&avatar, priority[i], 0, false);
        assert(pet_avatar_tick(&avatar, priority[i], 1000, true));
        lv_refr_now(display);
        memcpy(baseline, pixels, sizeof(pixels));
        uint32_t plain = hash();
        pet_avatar_set_environment(&avatar, PET_GROWTH_ENV_HOT);
        lv_refr_now(display);
        assert(avatar.pose.state == priority[i] && hash() != plain);
        /* 左侧环境符号可改变，主体的生病/睡眠表现必须逐像素保留。 */
        for (int y = 0; y < 320; ++y) for (int x = 70; x < 240; ++x)
            assert(pixels[y * 240 + x] == baseline[y * 240 + x]);
    }
    pet_avatar_set_environment(&avatar, PET_GROWTH_ENV_NONE);
    pet_avatar_tick(&avatar, PET_GROWTH_IDLE, 4400, false);
    lv_refr_now(display);
    unsigned before = flushes;
    assert(!pet_avatar_tick(&avatar, PET_GROWTH_IDLE, 4500, false));
    lv_refr_now(display);
    assert(flushes == before); /* 遮挡时无动画刷新。 */
    assert(pet_avatar_tick(&avatar, PET_GROWTH_HUNGRY, 4600, true));
    lv_refr_now(display);
    pet_avatar_set_appearance(&avatar,&(pet_appearance_t){.outfit=1},true);
    for(unsigned reaction=PET_REACT_ATTEND;reaction<=PET_REACT_WAVE;++reaction) {
        pet_avatar_tick(&avatar,PET_GROWTH_IDLE,0,false);
        pet_avatar_react(&avatar,(pet_reaction_t)reaction,0);
        for(unsigned frame=0;frame<42;++frame) {
            pet_avatar_tick(&avatar,PET_GROWTH_IDLE,frame*100,true);lv_refr_now(display);
            export_frame(argv[1],11+reaction,frame);
        }
    }
    for(unsigned rare=0;rare<6;++rare) {
        pet_avatar_tick(&avatar,PET_GROWTH_IDLE,0,false);
        pet_avatar_tick(&avatar,PET_GROWTH_IDLE,0,true);
        for(unsigned frame=0;frame<42;++frame) {
            pet_avatar_tick(&avatar,PET_GROWTH_IDLE,27500+rare*30000+frame*50,true);lv_refr_now(display);
            export_frame(argv[1],24+rare,frame);
        }
    }
    lv_display_delete(display);
    lv_deinit();
    puts("pet avatar: eight states, four environment overlays, bounded animation and suspend passed");
}
