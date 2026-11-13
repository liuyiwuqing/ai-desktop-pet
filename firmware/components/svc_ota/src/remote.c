#include "svc_ota/remote.h"
#include "pet/json_guard.h"
#include "pet/nexus.h"
#include "nvs.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOURNAL_MAX 1200u
#define HEADER 16u
struct pet_ota_remote {
    char device[41],boot[33],source_boot[33],request[41],reboot[41],state[24];
    uint32_t issued,report,written;
    esp_err_t error;
    bool writable,present;
    pet_ota_target_t target;
    const pet_ota_identity_t *identity;
    pet_ota_remote_port_t port;
};
static const cJSON *get(const cJSON *value,const char *name)
{return cJSON_GetObjectItemCaseSensitive(value,name);}
static bool keys(const cJSON *value,const char *const *names,size_t count)
{
    if(!cJSON_IsObject(value)||(size_t)cJSON_GetArraySize(value)!=count)return false;
    for(const cJSON *p=value->child;p;p=p->next) {
        size_t i=0;while(i<count&&strcmp(p->string,names[i]))++i;
        if(i==count)return false;
        for(const cJSON *q=p->next;q;q=q->next)if(!strcmp(p->string,q->string))return false;
    }
    return true;
}
static bool token(const cJSON *value,size_t limit,bool empty)
{
    if(!cJSON_IsString(value)||(!empty&&!value->valuestring[0])||strlen(value->valuestring)>limit)return false;
    for(const char *p=value->valuestring;*p;++p)
        if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='-'||*p=='_'))return false;
    return true;
}
static bool hex(const char *value,size_t length)
{
    if(!value||strlen(value)!=length)return false;
    for(size_t i=0;i<length;++i)if(!((value[i]>='0'&&value[i]<='9')||(value[i]>='a'&&value[i]<='f')))return false;
    return true;
}
static bool number(const cJSON *value,uint32_t minimum,uint32_t maximum)
{return cJSON_IsNumber(value)&&isfinite(value->valuedouble)&&value->valuedouble>=minimum&&
    value->valuedouble<=maximum&&floor(value->valuedouble)==value->valuedouble;}
static cJSON *parse(const char *wire,size_t bytes)
{
    if(!pet_json_bounded(wire,bytes,4))return NULL;
    const char *end=NULL;cJSON *root=cJSON_ParseWithLengthOpts(wire,bytes,&end,false);
    while(root&&end<wire+bytes&&(*end==' '||*end=='\n'||*end=='\r'||*end=='\t'))++end;
    if(root&&end==wire+bytes)return root;
    cJSON_Delete(root);return NULL;
}
static bool target_parse(const cJSON *value,pet_ota_target_t *out)
{
    static const char *const names[]={"releaseId","version","imageSha256","imageSize","releaseSequence"};
    if(!keys(value,names,5)||!token(get(value,"releaseId"),64,false)||
        !cJSON_IsString(get(value,"version"))||!get(value,"version")->valuestring[0]||
        strlen(get(value,"version")->valuestring)>32||!cJSON_IsString(get(value,"imageSha256"))||
        !hex(get(value,"imageSha256")->valuestring,64)||!number(get(value,"imageSize"),1,4*1024*1024)||
        !number(get(value,"releaseSequence"),1,UINT32_MAX))return false;
    const char *version=get(value,"version")->valuestring;
    for(const char *p=version;*p;++p)
        if(!((*p>='0'&&*p<='9')||(*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||*p=='.'||*p=='_'||*p=='-'))return false;
    memset(out,0,sizeof(*out));strcpy(out->release_id,get(value,"releaseId")->valuestring);
    strcpy(out->version,version);strcpy(out->image_sha256,get(value,"imageSha256")->valuestring);
    out->image_size=(uint32_t)get(value,"imageSize")->valuedouble;
    out->sequence=(uint32_t)get(value,"releaseSequence")->valuedouble;return true;
}
static cJSON *target_json(const pet_ota_target_t *target)
{
    cJSON *out=cJSON_CreateObject();if(!out)return NULL;
    cJSON_AddStringToObject(out,"releaseId",target->release_id);cJSON_AddStringToObject(out,"version",target->version);
    cJSON_AddStringToObject(out,"imageSha256",target->image_sha256);cJSON_AddNumberToObject(out,"imageSize",target->image_size);
    cJSON_AddNumberToObject(out,"releaseSequence",target->sequence);
    if(cJSON_GetArraySize(out)!=5){cJSON_Delete(out);return NULL;}
    return out;
}
static bool same(const pet_ota_target_t *a,const pet_ota_target_t *b)
{return a->sequence==b->sequence&&a->image_size==b->image_size&&!strcmp(a->release_id,b->release_id)&&
    !strcmp(a->version,b->version)&&!strcmp(a->image_sha256,b->image_sha256);}
static bool terminal(const char *state)
{return !strcmp(state,"healthy")||!strcmp(state,"failed")||!strcmp(state,"interrupted")||!strcmp(state,"rolled_back");}
static uint32_t read32(const unsigned char *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t x)
{for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(x>>(8*i));}
static uint32_t crc(const unsigned char *data,size_t bytes)
{
    uint32_t c=UINT32_MAX;
    for(size_t i=0;i<bytes;++i) {
        c^=i>=12&&i<16?0:data[i];
        for(unsigned b=0;b<8;++b)c=(c>>1)^(0xedb88320u&(0u-(c&1u)));
    }
    return c^UINT32_MAX;
}
static esp_err_t persist(pet_ota_remote_t *s)
{
    unsigned char data[JOURNAL_MAX+HEADER]="POTA\1",check[sizeof(data)];
    cJSON *root=cJSON_CreateObject();if(!root){s->writable=false;s->error=ESP_ERR_NO_MEM;return ESP_ERR_NO_MEM;}
    cJSON_AddStringToObject(root,"requestId",s->request);cJSON_AddStringToObject(root,"bootId",s->source_boot);
    cJSON_AddStringToObject(root,"rebootId",s->reboot);cJSON_AddStringToObject(root,"state",s->state);
    cJSON_AddNumberToObject(root,"code",s->error);
    cJSON_AddNumberToObject(root,"issuedAt",s->issued);cJSON_AddItemToObject(root,"target",target_json(&s->target));
    bool ok=cJSON_GetArraySize(root)==7&&cJSON_IsObject(get(root,"target"))&&
        cJSON_PrintPreallocated(root,(char *)data+HEADER,JOURNAL_MAX,false);cJSON_Delete(root);
    if(!ok){s->writable=false;s->error=ESP_ERR_NO_MEM;return ESP_ERR_NO_MEM;}
    size_t bytes=strlen((char *)data+HEADER)+HEADER,count=0;
    put32(data+8,(uint32_t)(bytes-HEADER));put32(data+12,crc(data,bytes));
    (void)s->port.save(s->port.context,data,bytes);
    if(s->port.load(s->port.context,check,sizeof(check),&count)!=ESP_OK||count!=bytes||memcmp(data,check,bytes)) {
        s->writable=false;s->error=ESP_ERR_INVALID_STATE;return ESP_ERR_INVALID_STATE;
    }
    s->present=true;return ESP_OK;
}
static esp_err_t transition(pet_ota_remote_t *s,const char *state)
{strcpy(s->state,state);return persist(s);}
static esp_err_t recover(pet_ota_remote_t *s)
{
    pet_ota_image_state_t image=s->port.running(s->port.context,&s->target);
    if(image==PET_OTA_IMAGE_HEALTHY){s->error=ESP_OK;return transition(s,"healthy");}
    if(image==PET_OTA_IMAGE_PENDING){s->error=ESP_OK;return transition(s,"verifying");}
    if(image==PET_OTA_IMAGE_UNKNOWN)return transition(s,"unconfirmed");
    return transition(s,!strcmp(s->state,"installing")?"interrupted":"rolled_back");
}
esp_err_t pet_ota_remote_init(pet_ota_remote_t **out,const char *device,const char *boot,
    const pet_ota_identity_t *identity,const pet_ota_remote_port_t *port)
{
    if(!out||*out||!pet_nexus_device_id_valid(device)||!hex(boot,32)||!identity||!port||
        !port->load||!port->save||!port->apply||!port->reboot||!port->snapshot||!port->running||!port->allowed)return ESP_ERR_INVALID_ARG;
    pet_ota_remote_t *s=calloc(1,sizeof(*s));if(!s)return ESP_ERR_NO_MEM;
    *out=s;strcpy(s->device,device);strcpy(s->boot,boot);strcpy(s->state,"idle");s->identity=identity;s->port=*port;
    unsigned char data[JOURNAL_MAX+HEADER];size_t bytes=0;
    esp_err_t error=port->load(port->context,data,sizeof(data),&bytes);
    if(error==ESP_ERR_NVS_NOT_FOUND){s->writable=true;return ESP_OK;}
    if(error!=ESP_OK){s->error=error;return error;}
    if(bytes<=HEADER||bytes>sizeof(data)||memcmp(data,"POTA\1\0\0\0",8)||read32(data+8)!=bytes-HEADER||crc(data,bytes)!=read32(data+12))return ESP_ERR_INVALID_STATE;
    cJSON *root=parse((const char *)data+HEADER,bytes-HEADER);
    static const char *const names[]={"requestId","bootId","rebootId","state","issuedAt","target","code"};
    if(!keys(root,names,7)||!token(get(root,"requestId"),40,false)||!token(get(root,"rebootId"),40,true)||
        !cJSON_IsString(get(root,"bootId"))||!hex(get(root,"bootId")->valuestring,32)||
        !cJSON_IsString(get(root,"state"))||strlen(get(root,"state")->valuestring)>=sizeof(s->state)||
        !number(get(root,"issuedAt"),1,UINT32_MAX)||!cJSON_IsNumber(get(root,"code"))||
        !isfinite(get(root,"code")->valuedouble)||get(root,"code")->valuedouble<INT32_MIN||
        get(root,"code")->valuedouble>INT32_MAX||floor(get(root,"code")->valuedouble)!=get(root,"code")->valuedouble||
        !target_parse(get(root,"target"),&s->target)) {
        cJSON_Delete(root);return ESP_ERR_INVALID_STATE;
    }
    strcpy(s->request,get(root,"requestId")->valuestring);strcpy(s->source_boot,get(root,"bootId")->valuestring);
    strcpy(s->reboot,get(root,"rebootId")->valuestring);strcpy(s->state,get(root,"state")->valuestring);
    s->issued=(uint32_t)get(root,"issuedAt")->valuedouble;s->error=(esp_err_t)get(root,"code")->valuedouble;cJSON_Delete(root);
    if(!terminal(s->state)&&strcmp(s->state,"installing")&&strcmp(s->state,"pending_reboot")&&
        strcmp(s->state,"reboot_requested")&&strcmp(s->state,"verifying")&&strcmp(s->state,"unconfirmed"))return ESP_ERR_INVALID_STATE;
    s->writable=s->present=true;
    if((!terminal(s->state)||!strcmp(s->state,"healthy"))&&strcmp(s->source_boot,s->boot))return recover(s);
    return ESP_OK;
}
void pet_ota_remote_close(pet_ota_remote_t *s){free(s);}
esp_err_t pet_ota_remote_tick(pet_ota_remote_t *s)
{
    if(!s||!s->writable)return ESP_ERR_INVALID_STATE;
    if(!s->present||terminal(s->state))return ESP_OK;
    if(strcmp(s->source_boot,s->boot)) {
        pet_ota_image_state_t image=s->port.running(s->port.context,&s->target);
        const char *next=image==PET_OTA_IMAGE_HEALTHY?"healthy":image==PET_OTA_IMAGE_PENDING?"verifying":
            image==PET_OTA_IMAGE_OTHER?"rolled_back":"unconfirmed";
        return strcmp(s->state,next)?transition(s,next):ESP_OK;
    }
    if(!strcmp(s->state,"installing")) {
        pet_ota_snapshot_t progress;esp_err_t error=s->port.snapshot(s->port.context,&progress);
        if(error!=ESP_OK){s->error=error;return transition(s,"unconfirmed");}
        s->written=progress.written_bytes;
        if(progress.state==PET_OTA_PENDING_REBOOT&&progress.release_sequence==s->target.sequence)return transition(s,"pending_reboot");
        if(progress.state==PET_OTA_FAILED){s->error=progress.error;return transition(s,"failed");}
    }
    return ESP_OK;
}
esp_err_t pet_ota_remote_command(pet_ota_remote_t *s,const char *wire,size_t bytes,bool retained,
    uint32_t now,char *out,size_t capacity)
{
    if(!s||!out||capacity<256)return ESP_ERR_INVALID_ARG;
    char request[41]="",command[41]="";const char *operation="",*status="rejected";
    esp_err_t error=ESP_ERR_INVALID_ARG;bool accepted=false;cJSON *root=NULL;
    if(!wire||!bytes||bytes>1024||retained||!now)goto done;
    root=parse(wire,bytes);
    static const char *const names[]={"schemaVersion","deviceId","requestId","commandId","operation","bootId","issuedAt","expiresAt","target"};
    if(!keys(root,names,9)||!number(get(root,"schemaVersion"),1,1)||
        !cJSON_IsString(get(root,"deviceId"))||strcmp(get(root,"deviceId")->valuestring,s->device)||
        !token(get(root,"requestId"),40,false)||!token(get(root,"commandId"),40,false)||
        !cJSON_IsString(get(root,"operation")))goto done;
    operation=get(root,"operation")->valuestring;
    if(strcmp(operation,"install")&&strcmp(operation,"reboot")){operation="";goto done;}
    strcpy(request,get(root,"requestId")->valuestring);strcpy(command,get(root,"commandId")->valuestring);
    pet_ota_target_t target;
    if(!cJSON_IsString(get(root,"bootId"))||strcmp(get(root,"bootId")->valuestring,s->boot)||
        !number(get(root,"issuedAt"),1,UINT32_MAX)||!number(get(root,"expiresAt"),1,UINT32_MAX)||
        !target_parse(get(root,"target"),&target))goto done;
    error=ESP_ERR_INVALID_STATE;
    if(!s->writable){status="unavailable";goto done;}
    if(!strcmp(request,s->request)) {
        if(!same(&target,&s->target)){status="conflict";goto done;}
        if(!strcmp(operation,"install")) {
            if(strcmp(command,request)){status="conflict";goto done;}
            accepted=true;goto done;
        }
        if(s->reboot[0]) {
            if(strcmp(s->reboot,command)){status="conflict";goto done;}
            accepted=true;goto done;
        }
    }
    uint32_t issued=(uint32_t)get(root,"issuedAt")->valuedouble,until=(uint32_t)get(root,"expiresAt")->valuedouble;
    if(until<=issued||until-issued>300||issued>now||now>=until){status="expired";goto done;}
    if(!s->port.allowed(s->port.context)){status="unavailable";goto done;}
    if(!strcmp(operation,"install")) {
        pet_ota_snapshot_t progress;
        if(strcmp(command,request)||(!terminal(s->state)&&strcmp(s->state,"idle"))||
            (s->present&&issued<=s->issued)||s->port.snapshot(s->port.context,&progress)!=ESP_OK||
            progress.state==PET_OTA_DOWNLOADING||progress.state==PET_OTA_PENDING_REBOOT||target.sequence<=progress.release_sequence) {
            status="conflict";goto done;
        }
        strcpy(s->request,request);strcpy(s->source_boot,s->boot);s->reboot[0]=0;s->issued=issued;
        s->target=target;s->written=0;s->error=ESP_OK;
        error=transition(s,"installing");if(error!=ESP_OK)goto done;
        error=s->port.apply(s->port.context,&s->target);
        if(error!=ESP_OK){s->error=error;(void)transition(s,"failed");goto done;}
        accepted=true;
    } else {
        if(strcmp(request,s->request)||strcmp(s->state,"pending_reboot")||!strcmp(command,request)) {
            status="conflict";goto done;
        }
        strcpy(s->reboot,command);
        error=transition(s,"reboot_requested");if(error!=ESP_OK)goto done;
        error=s->port.reboot(s->port.context);
        // A failed enqueue remains ambiguous in the durable journal; never replay the action automatically.
        if(error!=ESP_OK){s->error=error;(void)transition(s,"unconfirmed");goto done;}
        accepted=true;
    }
done:
    if(accepted){error=ESP_OK;status="accepted";}
    int n=snprintf(out,capacity,"{\"schemaVersion\":1,\"requestId\":\"%s\",\"commandId\":\"%s\",\"operation\":\"%s\",\"accepted\":%s,\"status\":\"%s\",\"code\":%d}",
        request,command,operation,accepted?"true":"false",status,error);
    cJSON_Delete(root);return n>0&&(size_t)n<capacity?error:ESP_ERR_INVALID_SIZE;
}
esp_err_t pet_ota_remote_status(pet_ota_remote_t *s,uint32_t now,char *out,size_t capacity)
{
    if(!s||!out||!now||s->report==UINT32_MAX)return ESP_ERR_INVALID_ARG;
    pet_ota_snapshot_t progress={0};bool ready=s->port.snapshot(s->port.context,&progress)==ESP_OK;
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    cJSON_AddNumberToObject(root,"schemaVersion",1);cJSON_AddStringToObject(root,"deviceId",s->device);
    cJSON_AddNumberToObject(root,"observedAt",now);cJSON_AddNumberToObject(root,"reportSequence",++s->report);
    cJSON_AddStringToObject(root,"bootId",s->boot);cJSON_AddBoolToObject(root,"available",s->writable&&ready&&s->port.allowed(s->port.context));
    cJSON_AddStringToObject(root,"state",s->writable?s->state:"unavailable");cJSON_AddNumberToObject(root,"code",s->error);
    cJSON_AddStringToObject(root,"requestId",s->request);cJSON_AddStringToObject(root,"rebootId",s->reboot);
    cJSON_AddNumberToObject(root,"writtenBytes",s->written);cJSON_AddNumberToObject(root,"reservedSequence",progress.release_sequence);
    cJSON *id=cJSON_AddObjectToObject(root,"identity");
    cJSON_AddStringToObject(id,"project",s->identity->project);cJSON_AddStringToObject(id,"board",s->identity->board);
    cJSON_AddStringToObject(id,"modelSha256",s->identity->model_sha256);cJSON_AddStringToObject(id,"partitionSha256",s->identity->partition_sha256);
    cJSON_AddNumberToObject(id,"configSchema",s->identity->config_schema);cJSON_AddNumberToObject(id,"growthSchema",s->identity->growth_schema);
    if(s->present)cJSON_AddItemToObject(root,"target",target_json(&s->target));
    bool ok=cJSON_GetArraySize(root)==(s->present?14:13)&&cJSON_GetArraySize(id)==6&&
        (!s->present||cJSON_IsObject(get(root,"target")))&&cJSON_PrintPreallocated(root,out,(int)capacity,false);
    cJSON_Delete(root);
    return ok?ESP_OK:ESP_ERR_INVALID_SIZE;
}
