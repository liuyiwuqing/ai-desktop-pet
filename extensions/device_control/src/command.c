#include "pet_ext/command.h"
#include <string.h>
#include <stdio.h>
static bool token(const char *s,size_t cap)
{
    if(!s||!s[0])return false;
    size_t n=0;while(n<cap&&s[n])++n;if(n==cap)return false;
    for(;*s;++s)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'||*s=='.'))return false;
    return true;
}
static const pet_command_capability_t *find(const pet_command_engine_t *s,const pet_command_request_t *r)
{
    if(!r||!r->id||!token(r->device,sizeof(r->device))||!token(r->capability,sizeof(r->capability))||
        !memchr(r->argument,0,sizeof(r->argument)))return NULL;
    for(size_t i=0;i<s->count;++i) {
        const pet_command_capability_t *c=&s->capabilities[i];
        if(strcmp(c->device,r->device)||strcmp(c->name,r->capability))continue;
        if(!c->argument_count)return r->argument[0]?NULL:c;
        for(size_t j=0;j<c->argument_count;++j)if(!strcmp(c->arguments[j],r->argument))return c;
    }
    return NULL;
}
static pet_command_status_t result(pet_command_result_t *out,pet_command_status_t status)
{if(out)*out=(pet_command_result_t){.status=status};return status;}
static void audit(pet_command_engine_t *s,const pet_command_capability_t *c)
{s->audit[s->next_audit++%16]=(pet_command_audit_t){s->request.id,s->adapter.now_ms(s->adapter.context),c->risk,s->result.status};}
bool pet_command_init(pet_command_engine_t *s,const pet_command_capability_t *caps,size_t count,
    const pet_command_adapter_t *adapter,uint64_t persisted)
{
    if(!s||!caps||!count||count>16||!adapter||!adapter->now_ms||!adapter->execute||!adapter->nonce)return false;
    for(size_t i=0;i<count;++i) {
        if(!token(caps[i].device,32)||!token(caps[i].name,64)||(unsigned)caps[i].risk>PET_EXT_RISK_L2_DESTRUCTIVE||
            caps[i].timeout_ms<100||caps[i].timeout_ms>5000||caps[i].argument_count>16||
            (caps[i].argument_count&&!caps[i].arguments)||
            (caps[i].risk!=PET_EXT_RISK_L0_READ&&!adapter->reserve))return false;
        for(size_t j=0;j<caps[i].argument_count;++j)if(!token(caps[i].arguments[j],41))return false;
        for(size_t j=0;j<i;++j)if(!strcmp(caps[j].device,caps[i].device)&&!strcmp(caps[j].name,caps[i].name))return false;
    }
    *s=(pet_command_engine_t){.capabilities=caps,.count=count,.adapter=*adapter,.highest_id=persisted};return true;
}
static pet_command_status_t execute(pet_command_engine_t *s,const pet_command_capability_t *c,pet_command_result_t *out)
{
    s->pending=false;
    if(c->risk!=PET_EXT_RISK_L0_READ&&!s->adapter.reserve(s->adapter.context,s->request.id))
        s->result=(pet_command_result_t){.status=PET_COMMAND_UNAVAILABLE};
    else {
        uint64_t began=s->adapter.now_ms(s->adapter.context);
        s->result=(pet_command_result_t){.status=PET_COMMAND_UNKNOWN};
        pet_command_status_t status=s->adapter.execute(s->adapter.context,&s->request,c->timeout_ms,&s->result);
        uint64_t ended=s->adapter.now_ms(s->adapter.context);
        s->result.status=ended<began||ended-began>c->timeout_ms?PET_COMMAND_UNKNOWN:status;
        s->result.summary[sizeof(s->result.summary)-1]=0;
        if(s->result.status==PET_COMMAND_TIMEOUT)s->result.status=PET_COMMAND_UNKNOWN;
    }
    audit(s,c);*out=s->result;return out->status;
}
pet_command_status_t pet_command_submit(pet_command_engine_t *s,const pet_command_request_t *r,pet_command_result_t *out)
{
    if(!s||!s->count||!out)return PET_COMMAND_INVALID;
    const pet_command_capability_t *c=find(s,r);if(!c)return result(out,PET_COMMAND_DENIED);
    if(s->request.id==r->id) {
        if(strcmp(s->request.device,r->device)||strcmp(s->request.capability,r->capability)||
            strcmp(s->request.argument,r->argument)||s->request.turn_id!=r->turn_id)return result(out,PET_COMMAND_DENIED);
        if(s->pending&&s->adapter.now_ms(s->adapter.context)>=s->expires_ms) {
            s->pending=false;s->result=(pet_command_result_t){.status=PET_COMMAND_DENIED};audit(s,c);
        }
        *out=s->result;return out->status;
    }
    uint64_t now=s->adapter.now_ms(s->adapter.context);
    if(s->pending&&now<s->expires_ms)return result(out,PET_COMMAND_BUSY);
    s->pending=false;
    if(r->id<=s->highest_id)return result(out,PET_COMMAND_DENIED);
    if(now<s->rate_start_ms||now<s->l2_start_ms)return result(out,PET_COMMAND_DENIED);
    if(now-s->rate_start_ms>=60000){s->rate_start_ms=now;s->rate_count=0;}
    if(now-s->l2_start_ms>=600000){s->l2_start_ms=now;s->l2_count=0;}
    if(s->rate_count>=30||(c->risk==PET_EXT_RISK_L2_DESTRUCTIVE&&s->l2_count>=3))return result(out,PET_COMMAND_BUSY);
    ++s->rate_count;if(c->risk==PET_EXT_RISK_L2_DESTRUCTIVE)++s->l2_count;
    s->highest_id=r->id;s->request=*r;
    if(c->confirmation||c->risk==PET_EXT_RISK_L2_DESTRUCTIVE) {
        s->confirmation_nonce=s->adapter.nonce(s->adapter.context);
        if(!s->confirmation_nonce||now>UINT64_MAX-60000) {
            s->result=(pet_command_result_t){.status=PET_COMMAND_UNAVAILABLE};*out=s->result;return out->status;
        }
        s->pending=true;s->expires_ms=now+60000;s->result=(pet_command_result_t){.status=PET_COMMAND_PENDING};
        audit(s,c);*out=s->result;return out->status;
    }
    return execute(s,c,out);
}
pet_command_status_t pet_command_confirm(pet_command_engine_t *s,uint64_t id,uint64_t nonce,bool approved,pet_command_result_t *out)
{
    if(!s||!out)return PET_COMMAND_INVALID;
    if(!s->pending||s->request.id!=id||nonce!=s->confirmation_nonce)return result(out,PET_COMMAND_DENIED);
    const pet_command_capability_t *c=find(s,&s->request);uint64_t now=s->adapter.now_ms(s->adapter.context);
    if(!approved||now>=s->expires_ms||s->expires_ms-now>60000) {
        s->pending=false;s->result=(pet_command_result_t){.status=PET_COMMAND_DENIED};
        if(c)audit(s,c);
        *out=s->result;return out->status;
    }
    if(!c)return result(out,PET_COMMAND_DENIED);
    return execute(s,c,out);
}
bool pet_command_pending(pet_command_engine_t *s,pet_command_request_t *out,uint64_t *nonce)
{
    if(!s||!out||!nonce||!s->pending)return false;
    if(s->adapter.now_ms(s->adapter.context)>=s->expires_ms) {
        s->pending=false;s->result=(pet_command_result_t){.status=PET_COMMAND_DENIED};
        const pet_command_capability_t *c=find(s,&s->request);if(c)audit(s,c);return false;
    }
    *out=s->request;*nonce=s->confirmation_nonce;return true;
}
size_t pet_command_audit(const pet_command_engine_t *s,pet_command_audit_t *out,size_t cap)
{
    if(!s||!out)return 0;
    size_t count=s->next_audit<16?s->next_audit:16;if(cap<count)count=cap;
    for(size_t i=0;i<count;++i)out[i]=s->audit[(s->next_audit-count+i)%16];
    return count;
}
