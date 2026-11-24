#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "pet/growth.h"

#define PET_COMPANION_CATALOG_VERSION 1u
#define PET_COMPANION_ITEMS 12u
#define PET_COMPANION_MEMORIES 8u

typedef struct { uint8_t theme, outfit, slots[3]; } pet_appearance_t;
typedef struct {
    uint32_t sequence, epoch;
    uint8_t kind, detail, theme, outfit;
} pet_memory_t;
typedef struct {
    uint16_t owned, best[3], played[3], focus_completed;
    pet_appearance_t appearance;
    uint32_t appearance_revision, memory_sequence, last_request_hash, last_command_hash;
    uint8_t last_command[16];
    pet_memory_t memories[PET_COMPANION_MEMORIES];
} pet_companion_state_t;
typedef struct {
    pet_companion_state_t state;
    uint16_t eligible;
    uint8_t themes;
    bool writable;
} pet_companion_view_t;
typedef enum { PET_COMPANION_BUY, PET_COMPANION_DRESS, PET_COMPANION_GAME } pet_companion_op_t;
typedef struct {
    uint64_t request_id, issued_at_ms;
    uint32_t expected_revision;
    pet_companion_op_t operation;
    pet_appearance_t appearance;
    uint8_t item, game, command_id[16];
    uint16_t score;
    bool practice;
} pet_companion_request_t;
typedef struct { pet_growth_result_t result; bool rewarded; } pet_companion_result_t;
typedef struct { const char *id, *name, *requirement; uint16_t price; } pet_companion_item_t;

/* Item number 0 means empty. 1..6 are wearables, 7..12 are furnishings. */
const pet_companion_item_t *pet_companion_item(unsigned number);
const char *pet_companion_theme(unsigned number);
const char *pet_companion_memory_name(unsigned kind);
