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
static QueueSetHandle_t s_queue_set;
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
    if (s_queue_set != NULL) {
        vQueueDelete(s_queue_set);
        s_queue_set = NULL;
    }
}

/* 高优先级先出队；同一优先级保持 FIFO。 */
static bool receive_next(pet_event_t *event)
{
    for (int priority = PET_EVENT_PRIORITY_COUNT - 1; priority >= 0; priority--) {
        if (xQueueReceive(s_queues[priority], event, 0) == pdTRUE) {
            return true;
        }
    }

    QueueSetMemberHandle_t member = xQueueSelectFromSet(s_queue_set, portMAX_DELAY);
    if (member == NULL) {
        return false;
    }

    for (int priority = PET_EVENT_PRIORITY_COUNT - 1; priority >= 0; priority--) {
        if (member == (QueueSetMemberHandle_t)s_queues[priority]) {
            return xQueueReceive(s_queues[priority], event, 0) == pdTRUE;
        }
    }
    return false;
}

static void event_bus_task(void *arg)
{
    (void)arg;
    pet_event_t event;

    while (receive_next(&event)) {
        for (uint8_t i = 0; i < s_handler_count[event.id]; i++) {
            s_handlers[event.id][i](&event);
        }
    }
}

esp_err_t pet_event_bus_init(void)
{
    if (s_queue_set != NULL) {
        return ESP_OK;
    }

    s_queue_set = xQueueCreateSet(PET_EVENT_QUEUE_DEPTH);
    if (s_queue_set == NULL) {
        return ESP_ERR_NO_MEM;
    }

    for (int priority = 0; priority < PET_EVENT_PRIORITY_COUNT; priority++) {
        s_queues[priority] =
            xQueueCreate(s_depths[priority], sizeof(pet_event_t));
        if (s_queues[priority] == NULL ||
            xQueueAddToSet(s_queues[priority], s_queue_set) != pdPASS) {
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
    if (s_queue_set == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    pet_event_t queued = *event;
    queued.sequence = __atomic_add_fetch(&s_sequence, 1, __ATOMIC_RELAXED);

    if (xQueueSend(s_queues[queued.priority], &queued, 0) != pdTRUE) {
        __atomic_add_fetch(&s_dropped[queued.priority], 1, __ATOMIC_RELAXED);
        return ESP_ERR_NO_MEM;
    }

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
