#include "svc_config/api.h"
#include "pet/json_guard.h"
#include "pet/nexus.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"
static const cJSON *field(const cJSON *root,const char *key)
{return cJSON_GetObjectItemCaseSensitive(root,key);}
static bool integer(const cJSON *value,uint32_t max)
{return cJSON_IsNumber(value)&&isfinite(value->valuedouble)&&value->valuedouble>=0&&value->valuedouble<=max&&floor(value->valuedouble)==value->valuedouble;}
esp_err_t pet_config_remote(const char *device,const char *json,size_t bytes,bool retained,
    uint32_t now,char *out,size_t cap)
{
    if (!out||cap<128) return ESP_ERR_INVALID_ARG;
    esp_err_t error=ESP_ERR_INVALID_ARG;char request[41]="";const char *reason="rejected";
    cJSON *root=NULL;
    if (!pet_nexus_device_id_valid(device)||!json||!bytes||bytes>1200||retained||!now||
        !pet_json_bounded(json,bytes,4)) goto done;
    const char *end=NULL;root=cJSON_ParseWithLengthOpts(json,bytes,&end,false);
    while (root&&end<json+bytes&&(*end==' '||*end=='\n'||*end=='\t'||*end=='\r'))++end;
    if (!cJSON_IsObject(root)||end!=json+bytes||cJSON_GetArraySize(root)!=7) goto done;
    static const char *const names[]={"schemaVersion","deviceId","requestId","expectedRevision","issuedAt","expiresAt","config"};
    unsigned mask=0;
    for (const cJSON *p=root->child;p;p=p->next) {
        unsigned i=0;while(i<7&&strcmp(p->string,names[i]))++i;
        if(i==7||(mask&(1u<<i)))goto done;
        mask|=1u<<i;
    }
    const cJSON *id=field(root,"deviceId"),*rid=field(root,"requestId"),*expected=field(root,"expectedRevision"),
        *issued=field(root,"issuedAt"),*expires=field(root,"expiresAt"),*version=field(root,"schemaVersion");
    if (!integer(version,1)||version->valuedouble!=1||!cJSON_IsString(id)||strcmp(id->valuestring,device)||
        !cJSON_IsString(rid)||!rid->valuestring[0]||strlen(rid->valuestring)>40||!integer(expected,UINT32_MAX-1)||
        !integer(issued,UINT32_MAX)||!integer(expires,UINT32_MAX)) goto done;
    for (const char *p=rid->valuestring;*p;++p)
        if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_')) goto done;
    strcpy(request,rid->valuestring);
    uint32_t at=(uint32_t)issued->valuedouble,until=(uint32_t)expires->valuedouble;
    if (!at||until<=at||until-at>300||at>now||now>=until) {reason="expired";goto done;}
    char config[PET_CONFIG_JSON_MAX+1];pet_config_t candidate;pet_config_snapshot_t current;
    if (!cJSON_PrintPreallocated((cJSON *)field(root,"config"),config,sizeof(config),false)||
        pet_config_parse_json(config,strlen(config),&candidate)!=ESP_OK) goto done;
    error=pet_config_snapshot(&current);if(error!=ESP_OK)goto done;
    uint32_t revision=(uint32_t)expected->valuedouble;
    if (current.revision==revision+1&&current.writable) {
        char a[PET_CONFIG_JSON_MAX+1],b[PET_CONFIG_JSON_MAX+1];size_t na,nb;
        if (pet_config_format_json(&candidate,a,sizeof(a),&na)==ESP_OK&&
            pet_config_format_json(&current.config,b,sizeof(b),&nb)==ESP_OK&&na==nb&&!memcmp(a,b,na)) error=ESP_OK;
        else error=ESP_ERR_INVALID_STATE;
    } else error=pet_config_apply(&candidate,revision);
    reason=error==ESP_OK?"applied":"conflict_or_storage";
done:
    cJSON_Delete(root);
    pet_config_snapshot_t snapshot;uint32_t revision_now=pet_config_snapshot(&snapshot)==ESP_OK?snapshot.revision:0;
    int n=snprintf(out,cap,"{\"schemaVersion\":1,\"requestId\":\"%s\",\"status\":\"%s\",\"revision\":%lu,\"code\":%d}",
        request,reason,(unsigned long)revision_now,error);
    return n>=0&&(size_t)n<cap?error:ESP_ERR_INVALID_SIZE;
}
