#include <assert.h>
#include <stdio.h>
#include "control_confirmation.h"
int main(void)
{
    pet_control_confirmation_t s={0};
    assert(pet_control_confirmation_update(&s,1,10,true,true,0)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,1,10,false,true,1000)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,1,10,true,true,1010)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,1,10,false,true,1900)==PET_CONTROL_INPUT_CONFIRM);
    assert(pet_control_confirmation_update(&s,1,10,false,true,2000)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,2,11,false,true,2010)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,2,11,true,true,2020)==PET_CONTROL_INPUT_NONE);
    assert(pet_control_confirmation_update(&s,2,11,false,true,2100)==PET_CONTROL_INPUT_CANCEL);
    assert(pet_control_confirmation_update(&s,3,12,false,false,2200)==PET_CONTROL_INPUT_CANCEL);
    puts("Control confirmation: held-key isolation, fresh long press, short cancel and display-loss cancellation passed");
}
