#include "ui_lvgl/api.h"
#include "recovery_view.h"
#include "ui_owner.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern const uint8_t font16_start[] asm("_binary_pet_noto_sc_16_bin_start");
extern const uint8_t font16_end[] asm("_binary_pet_noto_sc_16_bin_end");
extern const uint8_t font24_start[] asm("_binary_pet_noto_sc_24_bin_start");
extern const uint8_t font24_end[] asm("_binary_pet_noto_sc_24_bin_end");
typedef struct {
    const pet_display_t *panel;
    const pet_button_input_t *button;
    pet_ui_recovery_restart_t restart;
    void *restart_context,*buffer;
    lv_display_t *display;
    lv_font_t *small,*large;
    pet_recovery_view_ui_t view;
    pet_recovery_presenter_t presenter;
    uint32_t frame_at;
    unsigned strips;
    bool failed,button_available,lv_ready;
} recovery_ui_t;
static uint32_t now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
static void flush(lv_display_t *display,const lv_area_t *area,uint8_t *pixels)
{
    recovery_ui_t *ui=lv_display_get_user_data(display);
    uint32_t elapsed=now_ms()-ui->frame_at;
    if(ui->failed||elapsed>=100)ui->failed=true;
    else {
        lv_draw_sw_rgb565_swap(pixels,(area->x2-area->x1+1)*(area->y2-area->y1+1));
        ui->failed=ui->panel->write(area->x1,area->y1,area->x2+1,area->y2+1,pixels,100-elapsed)!=ESP_OK;
        ++ui->strips;
    }
    lv_display_flush_ready(display);
}
static bool render(recovery_ui_t *ui)
{
    pet_recovery_view_ui_show(&ui->view,ui->presenter.prompt,ui->button_available);
    ui->failed=false;ui->strips=0;ui->frame_at=now_ms();
    lv_refr_now(ui->display);
    return !ui->failed&&ui->strips&&now_ms()-ui->frame_at<=100&&ui->panel->backlight(40)==ESP_OK;
}
static void recovery_task(void *argument)
{
    recovery_ui_t *ui=argument;
    if(ui->panel->init()!=ESP_OK)goto fail;
    ui->button_available=ui->button&&ui->button->init()==ESP_OK;
    if(!ui->button_available)ESP_LOGW("recovery_ui","BOOT unavailable; serial maintenance remains active");
    lv_init();ui->lv_ready=true;lv_tick_set_cb(now_ms);
    ui->small=lv_binfont_create_from_buffer((void *)font16_start,font16_end-font16_start);
    ui->large=lv_binfont_create_from_buffer((void *)font24_start,font24_end-font24_start);
    ui->buffer=heap_caps_malloc(240*20*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!ui->buffer)ui->buffer=heap_caps_malloc(240*20*2,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    ui->display=lv_display_create(240,320);
    if(!ui->small||!ui->large||!ui->buffer||!ui->display)goto fail;
    lv_display_set_user_data(ui->display,ui);lv_display_set_color_format(ui->display,LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(ui->display,ui->buffer,NULL,240*20*2,LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(ui->display,flush);lv_timer_pause(lv_display_get_refr_timer(ui->display));
    if(!pet_recovery_view_ui_init(&ui->view,lv_display_get_screen_active(ui->display),ui->small,ui->large))goto fail;
    pet_recovery_presenter_init(&ui->presenter,ui->button_available&&ui->button->pressed(),now_ms());
    if(!render(ui))goto fail;
    for(;;) {
        pet_recovery_prompt_t before=ui->presenter.prompt;
        bool retry=ui->button_available&&pet_recovery_presenter_step(&ui->presenter,ui->button->pressed(),true,now_ms());
        // Every changed instruction must reach the screen before another gesture can take effect.
        if((before!=ui->presenter.prompt||retry)&&!render(ui))goto fail;
        if(retry){ui->restart(ui->restart_context);goto fail;}
        vTaskDelay(pdMS_TO_TICKS(20));
    }
fail:
    ESP_LOGE("recovery_ui","screen recovery stopped; use physical serial maintenance");
    (void)ui->panel->backlight(0);
    if(ui->display)lv_display_delete(ui->display);
    if(ui->small)lv_binfont_destroy(ui->small);
    if(ui->large)lv_binfont_destroy(ui->large);
    if(ui->lv_ready)lv_deinit();
    heap_caps_free(ui->buffer);heap_caps_free(ui);
    pet_ui_owner_release();vTaskDelete(NULL);
}
esp_err_t pet_ui_recovery_start(const pet_display_t *display,const pet_button_input_t *button,
                                 pet_ui_recovery_restart_t restart,void *context)
{
    if(!display||!display->init||!display->write||!display->backlight||display->width!=240||display->height!=320||
       !restart||(button&&(!button->init||!button->pressed)))return ESP_ERR_INVALID_ARG;
    if(!pet_ui_owner_claim())return ESP_ERR_INVALID_STATE;
    recovery_ui_t *ui=heap_caps_calloc(1,sizeof(*ui),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!ui){pet_ui_owner_release();return ESP_ERR_NO_MEM;}
    ui->panel=display;ui->button=button;ui->restart=restart;ui->restart_context=context;
    if(xTaskCreate(recovery_task,"pet_recovery_ui",6144,ui,4,NULL)!=pdPASS) {
        heap_caps_free(ui);pet_ui_owner_release();return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
