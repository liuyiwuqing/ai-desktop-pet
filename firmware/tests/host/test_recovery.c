#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "recovery_engine.h"
int main(void)
{
    pet_recovery_record_t record;memset(&record,0xff,sizeof(record));pet_recovery_engine_t state;
    assert(!pet_recovery_begin(&state,&record,false,true));
    assert(!pet_recovery_begin(&state,&record,true,false));assert(record.failures==1);
    pet_recovery_tick(&state,true,0);pet_recovery_tick(&state,false,29999);
    pet_recovery_tick(&state,true,30000);pet_recovery_tick(&state,true,59999);
    assert(record.failures==1);pet_recovery_tick(&state,true,60000);assert(record.failures==0&&record.pending==0);
    assert(!pet_recovery_begin(&state,&record,true,false));assert(record.failures==1);
    assert(!pet_recovery_begin(&state,&record,true,false));assert(record.failures==2);
    assert(pet_recovery_begin(&state,&record,true,false));assert(record.failures==3);
    assert(pet_recovery_claim_ui(&state));
    assert(!pet_recovery_claim_ui(&state));
    assert(pet_recovery_begin(&state,&record,true,false)); // Even a display panic cannot re-enter its startup path.
    assert(!pet_recovery_claim_ui(&state));
    pet_recovery_tick(&state,true,100000);pet_recovery_tick(&state,true,200000);assert(state.active);
    assert(pet_recovery_begin(&state,&record,false,false));
    pet_recovery_clear(&state);assert(state.active); // The maintenance boot stays isolated until restart.
    assert(!pet_recovery_claim_ui(&state));
    assert(!pet_recovery_begin(&state,&record,false,false));
    assert(!pet_recovery_claim_ui(&state));
    assert(!pet_recovery_begin(&state,&record,true,false));
    assert(!pet_recovery_begin(&state,&record,true,false));
    assert(pet_recovery_begin(&state,&record,true,false));
    assert(!pet_recovery_begin(&state,&record,false,true)); // RTC history cannot survive a cold power-on.
    record.checksum^=1;assert(!pet_recovery_begin(&state,&record,true,false));assert(record.failures==0);
    puts("Recovery boot-loop isolation passed");return 0;
}
