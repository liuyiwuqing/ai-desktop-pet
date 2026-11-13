#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PET_RECOVERY_PROMPT_RELEASE,
    PET_RECOVERY_PROMPT_IDLE,
    PET_RECOVERY_PROMPT_ARM_RELEASE,
    PET_RECOVERY_PROMPT_CONFIRM,
    PET_RECOVERY_PROMPT_RESTART_RELEASE,
    PET_RECOVERY_PROMPT_RESTARTING
} pet_recovery_prompt_t;
typedef struct {
    pet_recovery_prompt_t prompt;
    uint32_t raw_since,pressed_since,confirmed_since,last_sample;
    bool raw_pressed,pressed;
} pet_recovery_presenter_t;

void pet_recovery_presenter_init(pet_recovery_presenter_t *,bool pressed,uint32_t now_ms);
/* Two fresh 3-second holds, with stable releases and a 10-second confirmation window.
 * A hidden/failed display invalidates the gesture. True is emitted only once, after release. */
bool pet_recovery_presenter_step(pet_recovery_presenter_t *,bool pressed,bool visible,uint32_t now_ms);
