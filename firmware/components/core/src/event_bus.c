#include "pet/event_bus.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const UBaseType_t s_depths[PET_EVENT_PRIORITY_COUNT] = {
    [PET_EVENT_PRIORITY_LOW] = 4,
    [PET_EVENT_PRIORITY_NORMAL] = 16,
    [PET_EVENT_PRIORITY_HIGH] = 8,
    [PET_EVENT_PRIORITY_CRITICAL] = 4,
};

static QueueHandle_t s_queues[PET_EVENT_PRIORITY_COUNT];
static TaskHandle_t s_task;
static pet_event_handler_t s_handlers[PET_EVT_COUNT][PET_EVENT_MAX_HANDLERS];
static uint8_t s_handler_count[PET_EVT_COUNT];
static uint32_t s_dropped[PET_EVENT_PRIORITY_COUNT];
static uint32_t s_sequence;

static void event_bus_destroy(void)
{
    for (int priority = 0; priority < PET_EVENT_PRIORITY_COUNT; priority++) {
        if (s_queues[priority] != NULL) {
            vQueueDelete(s_queues[priority]);
            s_queues[priority] = NULL;
        }
    }
}

/* 高优先级先出队；同一优先级保持 FIFO。 */
static void receive_next(pet_event_t *event)
{
    for (;;) {
        for (int priority = PET_EVENT_PRIORITY_COUNT - 1; priority >= 0; priority--) {
            if (xQueueReceive(s_queues[priority], event, 0) == pdTRUE) {
                return;
            }
        }
        /* Queue Set 必须先 select 再读对应队列，与跨队列优先读取不兼容。
         * 通知只唤醒拥有者，队列才是数据来源；合并/迟到通知后重新扫描。
         * 发布发生在扫描和等待之间时，保留的通知让等待立即返回。 */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}

static void event_bus_task(void *arg)
{
    (void)arg;
    pet_event_t event;

    for (;;) {
        receive_next(&event);
        for (uint8_t i = 0; i < s_handler_count[event.id]; i++) {
            s_handlers[event.id][i](&event);
        }
    }
}

esp_err_t pet_event_bus_init(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }

    for (int priority = 0; priority < PET_EVENT_PRIORITY_COUNT; priority++) {
        s_queues[priority] =
            xQueueCreate(s_depths[priority], sizeof(pet_event_t));
        if (s_queues[priority] == NULL) {
            event_bus_destroy();
            return ESP_ERR_NO_MEM;
        }
    }

    if (xTaskCreate(event_bus_task, "pet_events", 3072, NULL, 5, &s_task) != pdPASS) {
        event_bus_destroy();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler)
{
    if (id < PET_EVT_BOOT || id >= PET_EVT_COUNT || handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_handler_count[id] >= PET_EVENT_MAX_HANDLERS) {
        return ESP_ERR_NO_MEM;
    }

    s_handlers[id][s_handler_count[id]++] = handler;
    return ESP_OK;
}

esp_err_t pet_event_bus_publish(const pet_event_t *event)
{
    if (!pet_event_is_valid(event)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pet_event_t queued = *event;
    queued.sequence = __atomic_add_fetch(&s_sequence, 1, __ATOMIC_RELAXED);

    if (xQueueSend(s_queues[queued.priority], &queued, 0) != pdTRUE) {
        __atomic_add_fetch(&s_dropped[queued.priority], 1, __ATOMIC_RELAXED);
        return ESP_ERR_NO_MEM;
    }

    (void)xTaskNotifyGive(s_task);
    return ESP_OK;
}

uint32_t pet_event_bus_dropped(void)
{
    uint32_t total = 0;
    for (int priority = 0; priority < PET_EVENT_PRIORITY_COUNT; priority++) {
        total += __atomic_load_n(&s_dropped[priority], __ATOMIC_RELAXED);
    }
    return total;
}

uint32_t pet_event_bus_dropped_by_priority(pet_event_priority_t priority)
{
    if (priority < PET_EVENT_PRIORITY_LOW || priority >= PET_EVENT_PRIORITY_COUNT) {
        return 0;
    }
    return __atomic_load_n(&s_dropped[priority], __ATOMIC_RELAXED);
}
