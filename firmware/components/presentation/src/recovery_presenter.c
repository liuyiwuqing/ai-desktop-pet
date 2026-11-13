#include "pet/presentation/recovery_presenter.h"

void pet_recovery_presenter_init(pet_recovery_presenter_t *s,bool pressed,uint32_t now)
{
    *s=(pet_recovery_presenter_t){.prompt=PET_RECOVERY_PROMPT_RELEASE,
        .raw_since=now,.pressed_since=now,.last_sample=now,.raw_pressed=pressed,.pressed=pressed};
}
bool pet_recovery_presenter_step(pet_recovery_presenter_t *s,bool pressed,bool visible,uint32_t now)
{
    if(!s||s->prompt==PET_RECOVERY_PROMPT_RESTARTING)return false;
    if(!visible||now-s->last_sample>INT32_MAX) {
        pet_recovery_presenter_init(s,pressed,now);return false;
    }
    s->last_sample=now;
    if(pressed!=s->raw_pressed){s->raw_pressed=pressed;s->raw_since=now;}
    bool edge=false;
    if(now-s->raw_since>=40&&s->pressed!=pressed){s->pressed=pressed;edge=true;}
    if((s->prompt==PET_RECOVERY_PROMPT_CONFIRM||s->prompt==PET_RECOVERY_PROMPT_RESTART_RELEASE)&&
       now-s->confirmed_since>=10000) {
        pet_recovery_presenter_init(s,pressed,now);return false;
    }
    switch(s->prompt) {
    case PET_RECOVERY_PROMPT_RELEASE:
        if(!pressed&&!s->pressed&&now-s->raw_since>=40)s->prompt=PET_RECOVERY_PROMPT_IDLE;
        break;
    case PET_RECOVERY_PROMPT_IDLE:
        if(edge&&s->pressed)s->pressed_since=now;
        if(s->pressed&&now-s->pressed_since>=3000)s->prompt=PET_RECOVERY_PROMPT_ARM_RELEASE;
        break;
    case PET_RECOVERY_PROMPT_ARM_RELEASE:
        if(edge&&!s->pressed){s->prompt=PET_RECOVERY_PROMPT_CONFIRM;s->confirmed_since=now;}
        break;
    case PET_RECOVERY_PROMPT_CONFIRM:
        if(edge&&s->pressed)s->pressed_since=now;
        if(edge&&!s->pressed)pet_recovery_presenter_init(s,pressed,now);
        else if(s->pressed&&now-s->pressed_since>=3000)s->prompt=PET_RECOVERY_PROMPT_RESTART_RELEASE;
        break;
    case PET_RECOVERY_PROMPT_RESTART_RELEASE:
        if(edge&&!s->pressed){s->prompt=PET_RECOVERY_PROMPT_RESTARTING;return true;}
        break;
    case PET_RECOVERY_PROMPT_RESTARTING:break;
    }
    return false;
}
