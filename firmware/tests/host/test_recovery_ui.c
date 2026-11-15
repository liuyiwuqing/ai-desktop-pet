#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_lvgl/api.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

/* Use the production binary fonts and LVGL. Only SDK scheduling, time, memory
 * and the injected panel/button are replaced at the hardware boundary. */
#ifdef __APPLE__
#define FONT_SECTION ".section __TEXT,__const\n"
#else
#define FONT_SECTION ".section .rodata\n"
#endif
__asm__(FONT_SECTION
    ".globl _binary_pet_noto_sc_16_bin_start\n_binary_pet_noto_sc_16_bin_start:\n"
    ".incbin \"" PET_FONT_DIR "/pet_noto_sc_16.bin\"\n"
    ".globl _binary_pet_noto_sc_16_bin_end\n_binary_pet_noto_sc_16_bin_end:\n"
    ".globl _binary_pet_noto_sc_24_bin_start\n_binary_pet_noto_sc_24_bin_start:\n"
    ".incbin \"" PET_FONT_DIR "/pet_noto_sc_24.bin\"\n"
    ".globl _binary_pet_noto_sc_24_bin_end\n_binary_pet_noto_sc_24_bin_end:\n");

static jmp_buf stopped;
static TaskFunction_t task_function;
static void *task_context;
static uint32_t clock_ms;
static unsigned allocations,writes,lit_frames,restarts;
static const char *scenario;
static bool fail_allocation,fail_task;

int64_t esp_timer_get_time(void){return (int64_t)clock_ms*1000;}
void *heap_caps_malloc(size_t size,unsigned caps)
{
    (void)caps;
    if(fail_allocation)return NULL;
    void *value=malloc(size);if(value)++allocations;return value;
}
void *heap_caps_calloc(size_t count,size_t size,unsigned caps)
{
    void *value=heap_caps_malloc(count*size,caps);if(value)memset(value,0,count*size);return value;
}
void heap_caps_free(void *value){if(value){assert(allocations);--allocations;free(value);}}
int xTaskCreate(TaskFunction_t function,const char *name,uint32_t stack,void *context,
                unsigned priority,TaskHandle_t *handle)
{
    (void)name;(void)stack;(void)priority;(void)handle;
    if(fail_task)return !pdPASS;
    task_function=function;task_context=context;return pdPASS;
}
void vTaskDelete(TaskHandle_t handle){assert(!handle);longjmp(stopped,1);}
void vTaskDelay(TickType_t ticks)
{
    clock_ms+=ticks;
    if(clock_ms>=10000) {
        if(!strcmp(scenario,"no-button")) {
            assert(lit_frames>0&&writes>0&&restarts==0);
            puts("Recovery UI: unavailable BOOT leaves visible USB maintenance without restart");
            fflush(stdout);_Exit(0);
        }
        assert(!"recovery task failed to stop");
    }
}
static bool pressed(void)
{
    return clock_ms<200||(clock_ms>=400&&clock_ms<3500)||(clock_ms>=3800&&clock_ms<6900);
}
static esp_err_t button_init(void){return !strcmp(scenario,"no-button")?ESP_FAIL:ESP_OK;}
static esp_err_t panel_init(void){return !strcmp(scenario,"init")?ESP_FAIL:ESP_OK;}
static esp_err_t panel_write(int x1,int y1,int x2,int y2,const void *pixels,uint32_t timeout)
{
    assert(x1>=0&&y1>=0&&x2<=240&&y2<=320&&x2>x1&&y2>y1&&pixels&&timeout>0&&timeout<=100);
    ++writes;
    return (!strcmp(scenario,"flush")||(!strcmp(scenario,"flush-final")&&clock_ms>=6940))?ESP_FAIL:ESP_OK;
}
static esp_err_t backlight(uint8_t percent)
{
    assert(percent==0||percent==40);
    if(percent) {
        if(!strcmp(scenario,"backlight-final")&&clock_ms>=6940)return ESP_FAIL;
        ++lit_frames;
    }
    return ESP_OK;
}
static void restart(void *context)
{
    assert(context==&restarts&&clock_ms>=6940&&!pressed()&&lit_frames>=6);
    ++restarts;
}
static esp_err_t ambient_read(uint16_t *raw)
{
    (void)raw;
    assert(0 && "recovery must not read optional sensors");
    return ESP_FAIL;
}
int main(int argc,char **argv)
{
    assert(argc==2);scenario=argv[1];
    const pet_display_t panel={.width=240,.height=320,.init=panel_init,
        .write=panel_write,.backlight=backlight,.ambient_read=ambient_read};
    const pet_button_input_t button={.init=button_init,.pressed=pressed};
    assert(pet_ui_recovery_start(NULL,&button,restart,&restarts)==ESP_ERR_INVALID_ARG);
    if(!strcmp(scenario,"allocation")||!strcmp(scenario,"task")) {
        fail_allocation=!strcmp(scenario,"allocation");fail_task=!strcmp(scenario,"task");
        assert(pet_ui_recovery_start(&panel,&button,restart,&restarts)==ESP_ERR_NO_MEM);
        assert(allocations==0);fail_allocation=fail_task=false;
    }
    assert(pet_ui_recovery_start(&panel,&button,restart,&restarts)==ESP_OK);
    assert(pet_ui_recovery_start(&panel,&button,restart,&restarts)==ESP_ERR_INVALID_STATE);
    if(!setjmp(stopped))task_function(task_context);
    assert(allocations==0);
    bool successful=!strcmp(scenario,"ok")||!strcmp(scenario,"allocation")||!strcmp(scenario,"task");
    assert(restarts==(successful?1u:0u));
    // Cleanup releases the LVGL owner, including failed initialization/frame paths.
    fail_task=true;
    assert(pet_ui_recovery_start(&panel,&button,restart,&restarts)==ESP_ERR_NO_MEM);
    assert(allocations==0);
    printf("Recovery UI adapter: %s passed\n",scenario);return 0;
}
