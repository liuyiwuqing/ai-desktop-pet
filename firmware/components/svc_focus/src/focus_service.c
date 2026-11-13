#include "svc_focus/api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
static pet_focus_clock_t clock_source;
static pet_focus_t state;
esp_err_t pet_focus_init(pet_focus_clock_t clock)
{
    if (!clock) return ESP_ERR_INVALID_ARG;
    if (lock) return ESP_ERR_INVALID_STATE;
    lock=xSemaphoreCreateMutex();clock_source=clock;return lock?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t pet_focus_start(uint64_t id)
{
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);bool ok=pet_focus_begin_at(&state,id,clock_source());xSemaphoreGive(lock);
    return ok?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t change(unsigned action)
{
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=action==0?pet_focus_pause_at(&state,clock_source()):action==1?pet_focus_resume_at(&state,clock_source()):true;
    if (action==2) pet_focus_cancel(&state);
    xSemaphoreGive(lock);return ok?ESP_OK:ESP_ERR_INVALID_STATE;
}
esp_err_t pet_focus_pause(void) {return change(0);}
esp_err_t pet_focus_resume(void) {return change(1);}
esp_err_t pet_focus_stop(void) {return change(2);}
esp_err_t pet_focus_snapshot(pet_focus_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    if (!lock||xSemaphoreTake(lock,0)!=pdTRUE) return ESP_ERR_INVALID_STATE;
    *out=state;
    if (out->state==PET_FOCUS_RUNNING) {
        uint64_t now=clock_source();out->remaining_ms=now>=out->deadline_ms?0:out->deadline_ms-now;
    }
    xSemaphoreGive(lock);return ESP_OK;
}
uint64_t pet_focus_tick(void)
{
    if (!lock) return 0;
    xSemaphoreTake(lock,portMAX_DELAY);(void)pet_focus_step(&state,clock_source());
    uint64_t id=state.reward_pending?state.event_id:0;xSemaphoreGive(lock);return id;
}
void pet_focus_reward_settled(uint64_t id)
{
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);if (state.event_id==id) pet_focus_settled(&state);xSemaphoreGive(lock);
}

esp_err_t pet_focus_begin(uint32_t epoch)
{
    if (epoch<1577836800) return ESP_ERR_INVALID_STATE;
    if (!lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(lock,portMAX_DELAY);
    uint64_t id=(uint64_t)epoch<<32;
    if (id<=state.event_id) id=state.event_id==UINT64_MAX?0:state.event_id+1;
    bool ok=pet_focus_begin_at(&state,id,clock_source());xSemaphoreGive(lock);
    return ok?ESP_OK:ESP_ERR_INVALID_STATE;
}
