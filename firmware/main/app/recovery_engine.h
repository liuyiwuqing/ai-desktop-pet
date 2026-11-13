#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t magic;
    uint8_t failures,pending,safe,ui_attempted;
    uint32_t checksum;
} pet_recovery_record_t;
typedef struct {
    pet_recovery_record_t *record;
    uint64_t healthy_since;
    bool active,tracking,settled;
} pet_recovery_engine_t;
/* RTC retention covers panic/watchdog restarts, not power loss. No NVS writes. */
bool pet_recovery_begin(pet_recovery_engine_t *,pet_recovery_record_t *,bool crash_reset,bool cold_boot);
void pet_recovery_tick(pet_recovery_engine_t *,bool healthy,uint64_t now_ms);
/* Clears the next boot's latch; the current maintenance boot remains isolated. */
void pet_recovery_clear(pet_recovery_engine_t *);
/* Persist before entering any display driver; a crash there falls back to USB next boot. */
bool pet_recovery_claim_ui(pet_recovery_engine_t *);
