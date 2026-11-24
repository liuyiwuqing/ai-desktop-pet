#include "svc_growth/companion_sync.h"
#include "svc_growth/api.h"
#include "growth_engine.h"
#include "pet/json_guard.h"
#include "pet/nexus.h"
#include "cJSON.h"
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool append(char *out,size_t cap,size_t *used,const char *format,...)
{
    if(*used>=cap)return false;
    va_list args;va_start(args,format);int n=vsnprintf(out+*used,cap-*used,format,args);va_end(args);
    if(n<0 || (size_t)n>=cap-*used) {out[0]='\0';*used=cap;return false;}
    *used+=(size_t)n;return true;
}
int pet_companion_sync_format(const char *device,const char *boot,uint32_t sequence,uint32_t epoch,
    uint32_t revision,const pet_companion_view_t *v,char *out,size_t cap)
{
    if(!out||!cap||!v||!pet_nexus_device_id_valid(device)||!boot||strlen(boot)!=32||!sequence||!epoch||
        !pet_companion_valid(&v->state)||v->eligible>4095||!(v->themes&1)||v->themes>7)return -1;
    for(unsigned i=0;i<32;++i)if(!((boot[i]>='0'&&boot[i]<='9')||(boot[i]>='a'&&boot[i]<='f')))return -1;
    const pet_companion_state_t *s=&v->state;size_t used=0;char last[33];
    for(unsigned i=0;i<16;++i)(void)snprintf(last+2*i,3,"%02x",s->last_command[i]);
    if(!append(out,cap,&used,"{\"schemaVersion\":1,\"catalogVersion\":1,\"deviceId\":\"%s\",\"bootId\":\"%s\",\"reportSequence\":%" PRIu32
        ",\"observedAt\":%" PRIu32 ",\"revision\":%" PRIu32 ",\"appearanceRevision\":%" PRIu32
        ",\"owned\":%u,\"eligible\":%u,\"themes\":%u,\"writable\":%s,\"lastCommand\":\"%s\","
        "\"appearance\":{\"theme\":%u,\"outfit\":%u,\"slots\":[%u,%u,%u]},"
        "\"best\":[%u,%u,%u],\"played\":[%u,%u,%u],\"focusCompleted\":%u,\"memories\":[",
        device,boot,sequence,epoch,revision,s->appearance_revision,s->owned,v->eligible,v->themes,v->writable?"true":"false",last,
        s->appearance.theme,s->appearance.outfit,s->appearance.slots[0],s->appearance.slots[1],s->appearance.slots[2],
        s->best[0],s->best[1],s->best[2],s->played[0],s->played[1],s->played[2],s->focus_completed))return -1;
    unsigned n=s->memory_sequence<8?s->memory_sequence:8;
    for(unsigned i=0;i<n;++i) {
        const pet_memory_t *m=&s->memories[(s->memory_sequence-i)%8];
        if(!append(out,cap,&used,"%s{\"sequence\":%" PRIu32 ",\"at\":%" PRIu32 ",\"kind\":%u,\"detail\":%u,\"theme\":%u,\"outfit\":%u}",
            i?",":"",m->sequence,m->epoch,m->kind,m->detail,m->theme,m->outfit))return -1;
    }
    return append(out,cap,&used,"]}")?(int)used:-1;
}
static const cJSON *field(const cJSON *root,const char *key) {return cJSON_GetObjectItemCaseSensitive(root,key);}
static bool number(const cJSON *n,uint32_t max) {return cJSON_IsNumber(n)&&isfinite(n->valuedouble)&&n->valuedouble>=0&&n->valuedouble<=max&&floor(n->valuedouble)==n->valuedouble;}
static bool fields(const cJSON *obj,const char *const *names,unsigned n)
{
    if(!cJSON_IsObject(obj)||cJSON_GetArraySize(obj)!=(int)n)return false;
    unsigned mask=0;
    for(const cJSON *p=obj->child;p;p=p->next) {
        unsigned i=0;while(i<n&&strcmp(p->string,names[i]))++i;
        if(i==n||(mask&(1u<<i)))return false;
        mask|=1u<<i;
    }
    return true;
}
pet_growth_result_t pet_companion_remote(const char *device,const char *json,size_t bytes,bool retained,
    uint32_t epoch,uint64_t monotonic_ms,char *reply,size_t cap)
{
    if(!reply||cap<256)return PET_GROWTH_INVALID;
    pet_growth_result_t result=PET_GROWTH_INVALID;const char *status="rejected";char id[37]="";
    cJSON *root=NULL;uint32_t applied_revision=0;
    if(!pet_nexus_device_id_valid(device)||!json||!bytes||bytes>PET_COMPANION_COMMAND_MAX||retained||!epoch||!pet_json_bounded(json,bytes,3))goto done;
    const char *end=NULL;root=cJSON_ParseWithLengthOpts(json,bytes,&end,false);
    while(root&&end<json+bytes&&(*end==' '||*end=='\n'||*end=='\r'||*end=='\t'))++end;
    static const char *const names[]={"schemaVersion","catalogVersion","deviceId","requestId","expectedRevision","issuedAt","expiresAt","appearance"};
    if(end!=json+bytes||!fields(root,names,8))goto done;
    const cJSON *rid=field(root,"requestId"),*did=field(root,"deviceId"),*version=field(root,"schemaVersion"),*catalog=field(root,"catalogVersion"),
        *expected=field(root,"expectedRevision"),*issued=field(root,"issuedAt"),*expires=field(root,"expiresAt"),*appearance=field(root,"appearance");
    if(!number(version,1)||version->valuedouble!=1||!number(catalog,1)||catalog->valuedouble!=1||
        !cJSON_IsString(did)||strcmp(did->valuestring,device)||!cJSON_IsString(rid)||!pet_nexus_uuid_valid(rid->valuestring)||
        !strcmp(rid->valuestring,"00000000-0000-0000-0000-000000000000")||!number(expected,UINT32_MAX-1)||!number(issued,UINT32_MAX)||!number(expires,UINT32_MAX))goto done;
    strcpy(id,rid->valuestring);
    uint32_t at=(uint32_t)issued->valuedouble,until=(uint32_t)expires->valuedouble;
    if(!at||until<=at||until-at>30||at>epoch||epoch>=until) {status="expired";result=PET_GROWTH_STALE_REQUEST;goto done;}
    static const char *const appearance_names[]={"theme","outfit","slots"};
    if(!fields(appearance,appearance_names,3)||!number(field(appearance,"theme"),2)||!number(field(appearance,"outfit"),6))goto done;
    const cJSON *slots=field(appearance,"slots");if(!cJSON_IsArray(slots)||cJSON_GetArraySize(slots)!=3)goto done;
    pet_companion_request_t request={.operation=PET_COMPANION_DRESS,.issued_at_ms=monotonic_ms,
        .expected_revision=(uint32_t)expected->valuedouble,
        .appearance={.theme=(uint8_t)field(appearance,"theme")->valuedouble,.outfit=(uint8_t)field(appearance,"outfit")->valuedouble}};
    for(unsigned i=0;i<3;++i) {const cJSON *slot=cJSON_GetArrayItem(slots,(int)i);if(!number(slot,12))goto done;request.appearance.slots[i]=(uint8_t)slot->valuedouble;}
    unsigned digits=0;
    for(const char *p=id;*p;++p)if(*p!='-') {
        unsigned nibble=*p<='9'?(unsigned)(*p-'0'):(unsigned)(*p-'a'+10);
        request.command_id[digits/2]=(uint8_t)((request.command_id[digits/2]<<4)|nibble);++digits;
    }
    pet_growth_view_t growth;result=pet_growth_get_snapshot(&growth);
    if(result!=PET_GROWTH_OK)goto done;
    request.request_id=growth.next_request_id;
    result=pet_companion_submit(&request).result;
    if(result==PET_GROWTH_OK||result==PET_GROWTH_DUPLICATE) {
        status="applied";result=PET_GROWTH_OK;applied_revision=request.expected_revision+1;
    } else if(result==PET_GROWTH_CONFLICT)status="conflict";
    else if(result==PET_GROWTH_STORAGE_UNCERTAIN)status="unconfirmed";
    else if(result==PET_GROWTH_STORAGE_ERROR)status="storage_error";
done:
    cJSON_Delete(root);
    int written=snprintf(reply,cap,"{\"schemaVersion\":1,\"requestId\":\"%s\",\"status\":\"%s\",\"appearanceRevision\":%" PRIu32 ",\"code\":%d}",id,status,applied_revision,result);
    return written>=0&&(size_t)written<cap?result:PET_GROWTH_INVALID;
}
