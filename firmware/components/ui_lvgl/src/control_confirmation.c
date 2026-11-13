#include "control_confirmation.h"
pet_control_input_result_t pet_control_confirmation_update(pet_control_confirmation_t *s,uint64_t id,uint64_t nonce,
    bool pressed,bool visible,uint32_t now)
{
    if(!s||!id||!nonce)return PET_CONTROL_INPUT_NONE;
    if(s->id!=id||s->nonce!=nonce) {
        *s=(pet_control_confirmation_t){.id=id,.nonce=nonce,.pressed=pressed,.ready=!pressed};
    }
    if(s->decided)return PET_CONTROL_INPUT_NONE;
    if(!visible){s->decided=true;return PET_CONTROL_INPUT_CANCEL;}
    if(!s->ready){if(!pressed)s->ready=true;s->pressed=pressed;return PET_CONTROL_INPUT_NONE;}
    pet_control_input_result_t result=PET_CONTROL_INPUT_NONE;
    if(pressed&&!s->pressed)s->pressed_ms=now;
    else if(!pressed&&s->pressed) {
        uint32_t duration=now-s->pressed_ms;
        if(duration>=800)result=PET_CONTROL_INPUT_CONFIRM;
        else if(duration>=30)result=PET_CONTROL_INPUT_CANCEL;
    }
    s->pressed=pressed;if(result!=PET_CONTROL_INPUT_NONE)s->decided=true;return result;
}
