#include "ui_lvgl/api.h"
#include "setup_view.h"
#include "ui_owner.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

extern const uint8_t font16_start[] asm("_binary_pet_noto_sc_16_bin_start");
extern const uint8_t font16_end[] asm("_binary_pet_noto_sc_16_bin_end");
extern const uint8_t font24_start[] asm("_binary_pet_noto_sc_24_bin_start");
extern const uint8_t font24_end[] asm("_binary_pet_noto_sc_24_bin_end");

typedef struct {
    const pet_display_t *panel;
    const pet_button_input_t *button;
    void *buffer;
    lv_display_t *display;
    lv_font_t *small,*large;
    pet_setup_view_t view;
    uint32_t frame_at;
    bool failed;
} setup_ui_t;

static uint32_t now_ms(void) {return (uint32_t)(esp_timer_get_time()/1000);}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{
    setup_ui_t *ui=lv_display_get_user_data(display);
    uint32_t elapsed=now_ms()-ui->frame_at;
    if(ui->failed||elapsed>=100) ui->failed=true;
    else {
        lv_draw_sw_rgb565_swap(pixels,(area->x2-area->x1+1)*(area->y2-area->y1+1));
        ui->failed=ui->panel->write(area->x1,area->y1,area->x2+1,area->y2+1,pixels,100-elapsed)!=ESP_OK;
    }
    lv_display_flush_ready(display);
}

static void task(void *argument)
{
    setup_ui_t *ui=argument;bool lv_ready=false;
    if(ui->panel->init()!=ESP_OK) goto fail;
    bool button_ready=ui->button&&ui->button->init()==ESP_OK;
    lv_init();lv_ready=true;lv_tick_set_cb(now_ms);
    ui->small=lv_binfont_create_from_buffer((void *)font16_start,font16_end-font16_start);
    ui->large=lv_binfont_create_from_buffer((void *)font24_start,font24_end-font24_start);
    ui->buffer=heap_caps_malloc(240*20*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    ui->display=lv_display_create(240,320);
    if(!ui->small||!ui->large||!ui->buffer||!ui->display) goto fail;
    lv_display_set_user_data(ui->display,ui);lv_display_set_color_format(ui->display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(ui->display,ui->buffer,NULL,240*20*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(ui->display,flush);lv_timer_pause(lv_display_get_refr_timer(ui->display));
    pet_setup_view_init(&ui->view,lv_display_get_screen_active(ui->display),ui->small,ui->large);
    pet_setup_snapshot_t old={0};uint32_t hold=0;bool armed=false,holding=false,rendered=false;
    for(;;) {
        pet_setup_snapshot_t current;
        if(pet_setup_snapshot(&current)!=ESP_OK) goto fail;
        if(!rendered||memcmp(&old,&current,sizeof(current))) {
            pet_setup_view_show(&ui->view,&current);ui->frame_at=now_ms();ui->failed=false;lv_refr_now(ui->display);
            if(ui->failed||ui->panel->backlight(80)!=ESP_OK) goto fail;
            old=current;rendered=true;
        }
        if(button_ready) {
            bool pressed=ui->button->pressed();
            if(!pressed) {armed=true;holding=false;}
            else if(armed) {
                if(!holding) {hold=now_ms();holding=true;}
                if(now_ms()-hold>=2000) {(void)pet_setup_leave();armed=false;}
            }
        }
        vTaskDelay(pdMS_TO_TICKS(40));
    }
fail:
    ESP_LOGE("setup_ui","display unavailable; closing setup and preserving saved data");
    (void)ui->panel->backlight(0);(void)pet_setup_leave();
    if(ui->display) lv_display_delete(ui->display);
    if(ui->small) lv_binfont_destroy(ui->small);
    if(ui->large) lv_binfont_destroy(ui->large);
    if(lv_ready) lv_deinit();
    heap_caps_free(ui->buffer);heap_caps_free(ui);pet_ui_owner_release();vTaskDelete(NULL);
}

esp_err_t pet_ui_setup_start(const pet_display_t *display,const pet_button_input_t *button)
{
    if(!display||!display->init||!display->write||!display->backlight||display->width!=240||display->height!=320||
       (button&&(!button->init||!button->pressed))) return ESP_ERR_INVALID_ARG;
    if(!pet_ui_owner_claim()) return ESP_ERR_INVALID_STATE;
    setup_ui_t *ui=heap_caps_calloc(1,sizeof(*ui),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!ui) {pet_ui_owner_release();return ESP_ERR_NO_MEM;}
    ui->panel=display;ui->button=button;
    if(xTaskCreate(task,"pet_setup_ui",8192,ui,4,NULL)!=pdPASS) {
        heap_caps_free(ui);pet_ui_owner_release();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
