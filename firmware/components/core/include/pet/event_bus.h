#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "pet/event.h"

/* 队列总深度 32，按优先级分队列：critical 4 / high 8 / normal 16 / low 4。 */
#define PET_EVENT_QUEUE_DEPTH 32
#define PET_EVENT_MAX_HANDLERS 16

typedef void (*pet_event_handler_t)(const pet_event_t *event);

esp_err_t pet_event_bus_init(void);
esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler);
esp_err_t pet_event_bus_publish(const pet_event_t *event);
uint32_t pet_event_bus_dropped(void);
uint32_t pet_event_bus_dropped_by_priority(pet_event_priority_t priority);
