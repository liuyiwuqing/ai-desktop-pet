#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "pet/presentation/recovery_presenter.h"

static void ready(pet_recovery_presenter_t *s,uint32_t t)
{
    pet_recovery_presenter_init(s,false,t);
    assert(!pet_recovery_presenter_step(s,false,true,t+40));
    assert(s->prompt==PET_RECOVERY_PROMPT_IDLE);
}
static void arm(pet_recovery_presenter_t *s,uint32_t t)
{
    assert(!pet_recovery_presenter_step(s,true,true,t));
    assert(!pet_recovery_presenter_step(s,true,true,t+40));
    assert(!pet_recovery_presenter_step(s,true,true,t+3039));
    assert(s->prompt==PET_RECOVERY_PROMPT_IDLE);
    assert(!pet_recovery_presenter_step(s,true,true,t+3040));
    assert(s->prompt==PET_RECOVERY_PROMPT_ARM_RELEASE);
    assert(!pet_recovery_presenter_step(s,false,true,t+3100));
    assert(!pet_recovery_presenter_step(s,false,true,t+3140));
    assert(s->prompt==PET_RECOVERY_PROMPT_CONFIRM);
}
int main(void)
{
    pet_recovery_presenter_t s;
    pet_recovery_presenter_init(&s,true,0);
    assert(!pet_recovery_presenter_step(&s,true,true,10000));
    assert(s.prompt==PET_RECOVERY_PROMPT_RELEASE); // Boot-held BOOT is never consent.
    ready(&s,0);
    assert(!pet_recovery_presenter_step(&s,true,true,100));
    assert(!pet_recovery_presenter_step(&s,false,true,120));
    assert(!pet_recovery_presenter_step(&s,false,true,160));
    assert(s.prompt==PET_RECOVERY_PROMPT_IDLE); // Contact bounce cannot arm a retry.
    arm(&s,200);
    assert(!pet_recovery_presenter_step(&s,true,true,3400));
    assert(!pet_recovery_presenter_step(&s,true,true,3440));
    assert(!pet_recovery_presenter_step(&s,true,true,6440));
    assert(s.prompt==PET_RECOVERY_PROMPT_RESTART_RELEASE);
    assert(!pet_recovery_presenter_step(&s,false,true,6500));
    assert(pet_recovery_presenter_step(&s,false,true,6540)); // Restart only after stable release.
    assert(!pet_recovery_presenter_step(&s,false,true,7000));
    ready(&s,10000);arm(&s,10100);
    assert(!pet_recovery_presenter_step(&s,false,true,23240));
    assert(s.prompt==PET_RECOVERY_PROMPT_RELEASE); // Confirmation expired.
    ready(&s,24000);arm(&s,24100);
    assert(!pet_recovery_presenter_step(&s,true,true,27400));
    assert(!pet_recovery_presenter_step(&s,true,true,27440));
    assert(!pet_recovery_presenter_step(&s,false,true,27500));
    assert(!pet_recovery_presenter_step(&s,false,true,27540));
    assert(s.prompt==PET_RECOVERY_PROMPT_RELEASE); // Short second press cancels.
    ready(&s,28000);arm(&s,28100);
    assert(!pet_recovery_presenter_step(&s,true,false,31400));
    assert(!pet_recovery_presenter_step(&s,true,true,35000));
    assert(s.prompt==PET_RECOVERY_PROMPT_RELEASE); // A lost frame invalidates consent.
    ready(&s,40000);arm(&s,40100);
    assert(!pet_recovery_presenter_step(&s,true,true,43239));
    assert(s.prompt==PET_RECOVERY_PROMPT_RELEASE); // Clock regression fails closed.
    uint32_t wrap=UINT32_MAX-1000;
    ready(&s,wrap);arm(&s,wrap+100); // Normal uint32_t wrap remains usable.
    puts("Recovery UI: held boot, debounce, two holds, release, timeout, visibility and clock boundaries passed");
    return 0;
}
