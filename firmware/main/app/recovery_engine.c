#include "recovery_engine.h"
#include <stddef.h>
#include <string.h>
#define RECOVERY_MAGIC UINT32_C(0x50424731)
static uint32_t checksum(const pet_recovery_record_t *record)
{
    const uint8_t *bytes=(const uint8_t *)record;uint32_t hash=2166136261u;
    for(size_t i=0;i<offsetof(pet_recovery_record_t,checksum);++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}
static void seal(pet_recovery_record_t *record){record->checksum=checksum(record);}
bool pet_recovery_begin(pet_recovery_engine_t *state,pet_recovery_record_t *record,bool crash,bool cold)
{
    bool valid=record->magic==RECOVERY_MAGIC&&record->checksum==checksum(record)&&
        record->failures<=3&&record->pending<=1&&record->safe<=1&&record->ui_attempted<=1;
    if(cold||!valid)*record=(pet_recovery_record_t){.magic=RECOVERY_MAGIC};
    else if(crash&&!record->safe) {
        record->failures=record->pending?(uint8_t)(record->failures+1):1;
        if(record->failures>=3){record->failures=3;record->safe=1;}
    } else if(!record->safe)record->failures=0;
    record->pending=1;seal(record);
    *state=(pet_recovery_engine_t){.record=record,.active=record->safe!=0};return state->active;
}
void pet_recovery_tick(pet_recovery_engine_t *state,bool healthy,uint64_t now)
{
    if(state->active||state->settled)return;
    if(!healthy|| (state->tracking&&now<state->healthy_since)){state->tracking=false;return;}
    if(!state->tracking){state->tracking=true;state->healthy_since=now;}
    if(now-state->healthy_since>=30000) {
        state->record->failures=0;state->record->pending=0;seal(state->record);state->settled=true;
    }
}
void pet_recovery_clear(pet_recovery_engine_t *state)
{
    *state->record=(pet_recovery_record_t){.magic=RECOVERY_MAGIC};seal(state->record);
}
bool pet_recovery_claim_ui(pet_recovery_engine_t *state)
{
    if(!state->active||!state->record->safe||state->record->ui_attempted)return false;
    state->record->ui_attempted=1;seal(state->record);return true;
}
