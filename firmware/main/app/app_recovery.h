#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {bool enabled,active,healthy,ui_attempted;uint8_t failures;uint32_t reset_reason;} app_recovery_snapshot_t;
bool app_recovery_start(void);
bool app_recovery_active(void);
void app_recovery_tick(bool healthy);
void app_recovery_clear(void);
bool app_recovery_claim_ui(void);
void app_recovery_snapshot(app_recovery_snapshot_t *out);
