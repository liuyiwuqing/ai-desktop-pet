#include "app_behavior.h"
#include "sdkconfig.h"
#include <time.h>
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "svc_focus/api.h"
#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "svc_growth/api.h"
#endif
static QueueHandle_t completed;
static uint64_t monotonic(void) {return (uint64_t)esp_timer_get_time()/1000;}
static portMUX_TYPE guard=portMUX_INITIALIZER_UNLOCKED;
static uint32_t lost;
static uint64_t completion_id(uint32_t sequence_id)
{
    time_t epoch=time(NULL);
    return epoch>=1577836800 && (uint64_t)epoch<=UINT32_MAX ? ((uint64_t)epoch<<32)|sequence_id : 0;
}
void app_behavior_dialogue(uint32_t turn)
{
    uint64_t id=completion_id(turn);
    if (!completed||!id||xQueueSend(completed,&id,0)!=pdTRUE) {
        portENTER_CRITICAL(&guard);++lost;portEXIT_CRITICAL(&guard);
    }
}
static bool reward(bool focus,uint64_t id)
{
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    pet_growth_view_t snapshot;
    if (pet_growth_get_snapshot(&snapshot)!=PET_GROWTH_OK) return false;
    pet_growth_result_t result=pet_growth_reward(focus?PET_GROWTH_REWARD_FOCUS:PET_GROWTH_REWARD_DIALOGUE,id,snapshot.task_day);
    return result==PET_GROWTH_OK||result==PET_GROWTH_DUPLICATE||result==PET_GROWTH_ALREADY_CLAIMED;
#else
    (void)focus;(void)id;return true;
#endif
}
static void worker(void *arg)
{
    (void)arg;uint64_t pending=0;uint32_t reported=0;
    for (;;) {
        if (!pending) (void)xQueueReceive(completed,&pending,pdMS_TO_TICKS(1000));
        if (pending&&reward(false,pending)) pending=0;
        uint64_t focus=pet_focus_tick();
        if (focus&&reward(true,focus)) pet_focus_reward_settled(focus);
        portENTER_CRITICAL(&guard);uint32_t dropped=lost;portEXIT_CRITICAL(&guard);
        if (dropped!=reported) {ESP_LOGW("behavior","completion queue dropped=%lu",(unsigned long)dropped);reported=dropped;}
        if (pending) vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
esp_err_t app_behavior_start(void)
{
    esp_err_t error=pet_focus_init(monotonic);if (error!=ESP_OK) return error;
    completed=xQueueCreate(16,sizeof(uint64_t));if (!completed) return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker,"pet_behavior",4096,NULL,2,NULL)!=pdPASS) {
        vQueueDelete(completed);completed=NULL;return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t app_focus_command(const char *action)
{
    if (!action) return ESP_ERR_INVALID_ARG;
    if (!strcmp(action,"pause")) return pet_focus_pause();
    if (!strcmp(action,"resume")) return pet_focus_resume();
    if (!strcmp(action,"cancel")) return pet_focus_stop();
    if (strcmp(action,"start")) return ESP_ERR_INVALID_ARG;
    time_t epoch=time(NULL);
    return epoch>=1577836800&&(uint64_t)epoch<=UINT32_MAX?pet_focus_begin((uint32_t)epoch):ESP_ERR_INVALID_STATE;
}
