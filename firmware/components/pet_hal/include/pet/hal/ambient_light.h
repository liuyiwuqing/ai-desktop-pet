#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t updated_ms;
    uint16_t filtered;
    uint8_t applied;
    bool initialized;
} pet_ambient_filter_t;

/* Raw relative light (0 dark, 4095 bright), not lux. The user's brightness is
 * an absolute ceiling; 0 stays off. Invalid input restores manual brightness. */
uint8_t pet_ambient_brightness(pet_ambient_filter_t *filter, uint16_t raw,
    bool valid, uint32_t now, uint8_t ceiling);
