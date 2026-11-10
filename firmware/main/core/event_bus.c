#include "event_bus.h"

#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static QueueHandle_t s_queue;
static TaskHandle_t s_task;
static pet_event_handler_t s_handlers[PET_EVT_COUNT][PET_EVENT_MAX_HANDLERS];
static uint8_t s_handler_count[PET_EVT_COUNT];
static uint32_t s_dropped;

static void event_bus_task(void *arg)
{
    (void)arg;
    pet_event_t event;

    while (xQueueReceive(s_queue, &event, portMAX_DELAY) == pdTRUE) {
        if (event.id >= PET_EVT_COUNT) {
            continue;
        }
        for (uint8_t i = 0; i < s_handler_count[event.id]; i++) {
            s_handlers[event.id][i](&event);
        }
    }
}

esp_err_t pet_event_bus_init(void)
{
    if (s_queue != NULL) {
        return ESP_OK;
    }

    s_queue = xQueueCreate(PET_EVENT_QUEUE_DEPTH, sizeof(pet_event_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(event_bus_task, "pet_events", 3072, NULL, 5, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler)
{
    if (id >= PET_EVT_COUNT || handler == NULL) {
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
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueSend(s_queue, event, 0) != pdTRUE) {
        s_dropped++;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

uint32_t pet_event_bus_dropped(void)
{
    return s_dropped;
}
