#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pet_ext/command.h"
static uint64_t now,watermark;static unsigned actions;static bool store_ok=true,timeout;
static uint64_t clock_ms(void *p){(void)p;return now;}
static uint64_t nonce(void *p){(void)p;return 1234567;}
static bool reserve(void *p,uint64_t id){(void)p;if(!store_ok)return false;watermark=id;return true;}
static pet_command_status_t execute(void *p,const pet_command_request_t *r,uint32_t ms,pet_command_result_t *out)
{(void)p;(void)r;++actions;assert(ms==1000);strcpy(out->summary,"accepted");if(timeout)now+=1001;return PET_COMMAND_OK;}
int main(void)
{
    static const char *const apps[]={"idea"};
    const pet_command_capability_t capabilities[]={{"mac","app.open",PET_EXT_RISK_L1_REVERSIBLE,true,apps,1,1000}};
    const pet_command_adapter_t adapter={NULL,execute,clock_ms,nonce,reserve};pet_command_engine_t s;
    assert(pet_command_init(&s,capabilities,1,&adapter,0));
    pet_command_request_t r={.id=1,.device="mac",.capability="app.open",.argument="idea"};pet_command_result_t out;
    assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_PENDING&&actions==0);
    assert(pet_command_confirm(&s,1,7,true,&out)==PET_COMMAND_DENIED&&actions==0);
    uint64_t token;pet_command_request_t pending;assert(pet_command_pending(&s,&pending,&token));
    assert(pet_command_confirm(&s,1,token,true,&out)==PET_COMMAND_OK&&actions==1&&watermark==1);
    assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_OK&&actions==1);
    strcpy(r.argument,"terminal");assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_DENIED&&actions==1);
    r.id=2;strcpy(r.argument,"idea");assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_PENDING);
    now=60001;assert(pet_command_confirm(&s,2,token,true,&out)==PET_COMMAND_DENIED&&actions==1);
    r.id=3;assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_PENDING);store_ok=false;
    assert(pet_command_confirm(&s,3,token,true,&out)==PET_COMMAND_UNAVAILABLE&&actions==1);
    store_ok=true;r.id=4;assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_PENDING);timeout=true;
    assert(pet_command_confirm(&s,4,token,true,&out)==PET_COMMAND_UNKNOWN&&actions==2);
    assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_UNKNOWN&&actions==2);
    pet_command_audit_t audit[16];assert(pet_command_audit(&s,audit,16)>0);
    assert(pet_command_init(&s,capabilities,1,&adapter,watermark));
    assert(pet_command_submit(&s,&r,&out)==PET_COMMAND_DENIED&&actions==2);
    puts("Commands: allowlist, local confirmation, expiry, durable reservation, timeout and replay passed");
}
