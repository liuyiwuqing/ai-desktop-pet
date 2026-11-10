#pragma once

#include <stdint.h>

#include "esp_err.h"

#include "pet/event.h"

#define PET_EVENT_QUEUE_DEPTH 32
#define PET_EVENT_MAX_HANDLERS 16

typedef void (*pet_event_handler_t)(const pet_event_t *event);

esp_err_t pet_event_bus_init(void);
esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler);
esp_err_t pet_event_bus_publish(const pet_event_t *event);
uint32_t pet_event_bus_dropped(void);
