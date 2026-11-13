#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum {PET_CONTROL_INPUT_NONE,PET_CONTROL_INPUT_CONFIRM,PET_CONTROL_INPUT_CANCEL} pet_control_input_result_t;
typedef struct {uint64_t id,nonce;uint32_t pressed_ms;bool pressed,ready,decided;} pet_control_confirmation_t;
/* 已经按住 BOOT 时弹出确认页，必须先松开再开始新手势。 */
pet_control_input_result_t pet_control_confirmation_update(pet_control_confirmation_t *,uint64_t id,uint64_t nonce,
    bool pressed,bool visible,uint32_t now_ms);
