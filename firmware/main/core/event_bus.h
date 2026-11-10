#pragma once

#include <stdint.h>

#include "esp_err.h"

#define PET_EVENT_QUEUE_DEPTH 32
#define PET_EVENT_MAX_HANDLERS 16
#define PET_EVENT_PAYLOAD_MAX 32

typedef enum {
    PET_EVT_BOOT = 0,
    PET_EVT_TICK,
    PET_EVT_FAULT,
    PET_EVT_COUNT,
} pet_event_id_t;

typedef struct {
    pet_event_id_t id;
    uint32_t timestamp_ms;
    uint16_t payload_len;
    uint8_t payload[PET_EVENT_PAYLOAD_MAX];
} pet_event_t;

typedef void (*pet_event_handler_t)(const pet_event_t *event);

esp_err_t pet_event_bus_init(void);
esp_err_t pet_event_bus_subscribe(pet_event_id_t id, pet_event_handler_t handler);
esp_err_t pet_event_bus_publish(const pet_event_t *event);
uint32_t pet_event_bus_dropped(void);
