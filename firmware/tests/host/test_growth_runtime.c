#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "pet/event_bus.h"
#include "svc_growth/api.h"

/* 调度边界允许新任务在 create 返回前执行；每次只运行到阻塞点。
 * 真实 runtime 的启动/路由/清理代码参与测试，不模拟 Flash 或声称覆盖 SMP 调度。 */
static const char *scenario;
static jmp_buf blocked;
static TaskFunction_t entry;
static void *entry_context;
static int task_token, queue_token;
static bool task_live, queue_live, queued, notified, in_worker;
static unsigned inits, closes, ticks, observations, overwrites;
static pet_climate_reading_t latest, observed;

static bool failing(const char *name) { return strcmp(scenario, name) == 0; }
static void run_once(void)
{
    in_worker = true;
    if (setjmp(blocked) == 0) entry(entry_context);
    in_worker = false;
}
int64_t esp_timer_get_time(void) { return 1000000; }
pet_growth_result_t pet_growth_init(const pet_growth_clock_t *clock)
{
    assert(clock->monotonic_ms(clock->context) == 1000 && clock->utc_offset_min == 480);
    ++inits;
    return failing("init-fail") ? PET_GROWTH_NOT_READY : PET_GROWTH_OK;
}
void pet_growth_close(void)
{
    assert(!task_live && !queue_live);
    ++closes;
}
pet_growth_result_t pet_growth_tick(void)
{
    assert(in_worker);
    ++ticks;
    return PET_GROWTH_OK;
}
pet_growth_result_t pet_growth_observe_climate(const pet_climate_reading_t *reading)
{
    assert(in_worker);
    ++observations;
    observed = *reading;
    return PET_GROWTH_OK;
}
int xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *context,
                unsigned priority, TaskHandle_t *task)
{
    (void)name; (void)stack; (void)priority;
    if (failing("task-fail")) return 0;
    task_live = true;
    entry = function;
    entry_context = context;
    *task = &task_token;
    run_once();
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task)
{
    assert(task == &task_token && task_live && ticks == 0);
    task_live = false;
}
void vTaskDelay(uint32_t delay)
{
    assert(delay == 1000 && in_worker);
    longjmp(blocked, 1);
}
uint32_t ulTaskNotifyTake(int clear, uint32_t timeout)
{
    assert(in_worker && clear == pdTRUE && timeout == portMAX_DELAY);
    if (!notified) longjmp(blocked, 1);
    return 1;
}
int xTaskNotifyGive(TaskHandle_t task)
{
    assert(task == &task_token && task_live && ticks == 0);
    notified = true;
    run_once();
    return pdPASS;
}
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    assert(length == 1 && item_size == sizeof(latest));
    if (failing("queue-fail")) return NULL;
    queue_live = true;
    return &queue_token;
}
void vQueueDelete(QueueHandle_t queue)
{
    assert(queue == &queue_token && queue_live && !task_live);
    queue_live = false;
}
int xQueueOverwrite(QueueHandle_t queue, const void *value)
{
    assert(queue == &queue_token && queue_live && !in_worker);
    memcpy(&latest, value, sizeof(latest));
    queued = true;
    ++overwrites;
    return pdPASS;
}
int xQueueReceive(QueueHandle_t queue, void *value, uint32_t timeout)
{
    assert(queue == &queue_token && queue_live && in_worker && timeout == 0);
    if (!queued) return 0;
    memcpy(value, &latest, sizeof(latest));
    queued = false;
    return pdTRUE;
}
esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler)
{
    assert(id == PET_EVT_CLIMATE_READING && handler && ticks == 0);
    if (failing("subscribe-fail")) return ESP_ERR_NO_MEM;
    pet_event_t event;
    pet_event_prepare(&event, PET_EVT_CLIMATE_READING, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_NORMAL);
    event.payload_len = sizeof(pet_climate_reading_t);
    handler(&event); /* 错来源。 */
    event.source = PET_EVENT_SOURCE_CLIMATE;
    event.payload_len = 1;
    handler(&event); /* 错长度。 */
    event.id = PET_EVT_CLIMATE_ALERT;
    event.payload_len = sizeof(pet_climate_reading_t);
    handler(&event); /* 错类型。 */
    assert(overwrites == 0);
    event.id = PET_EVT_CLIMATE_READING;
    for (unsigned timestamp = 123; timestamp <= 124; ++timestamp) {
        pet_climate_reading_t reading = {.temperature_c = 31, .humidity_pct = 90,
            .timestamp_ms = timestamp, .quality = 100, .valid = true};
        memcpy(event.payload, &reading, sizeof(reading));
        handler(&event);
    }
    assert(overwrites == 2 && observations == 0 && ticks == 0);
    return ESP_OK;
}
int main(int argc, char **argv)
{
    assert(argc <= 2);
    scenario = argc == 2 ? argv[1] : "success";
    bool success = failing("success");
    esp_err_t expected = success ? ESP_OK : failing("init-fail") ? ESP_FAIL : ESP_ERR_NO_MEM;
    assert(pet_growth_start() == expected && inits == 1);
    if (success) {
        assert(task_live && ticks == 1 && closes == 0);
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
        assert(observations == 1 && observed.timestamp_ms == 124 && overwrites == 2 && !queued);
#else
        assert(observations == 0 && !queue_live && overwrites == 0);
#endif
        assert(pet_growth_start() == ESP_OK && inits == 1 && ticks == 1);
    } else {
        assert(!task_live && !queue_live && ticks == 0 && observations == 0);
        assert(closes == (failing("init-fail") ? 0u : 1u));
    }
    printf("growth runtime: climate=%u %s passed\n", CONFIG_PET_MODULE_CLIMATE_ENABLED, scenario);
    return 0;
}
