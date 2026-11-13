#include "app_backup.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "nvs.h"
#include "svc_config/provision.h"
#include "svc_growth/api.h"
#include "cJSON.h"
#include "pet/nexus.h"
#ifndef CONFIG_PET_NET_DEVICE_ID
#define CONFIG_PET_NET_DEVICE_ID "pet-dev-01"
#endif

static int nibble(char c)
{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
esp_err_t app_backup_command(const char *line,size_t bytes,char *out,size_t cap)
{
    if (!line||!out||cap<128||bytes>1024) return ESP_ERR_INVALID_ARG;
    pet_growth_result_t result=PET_GROWTH_INVALID;
    char device[PET_NEXUS_DEVICE_ID_MAX+1];
    esp_err_t error=pet_provision_load_str("nexus","device_id",device,sizeof(device));
    if (error==ESP_ERR_NVS_NOT_FOUND) snprintf(device,sizeof(device),"%s",CONFIG_PET_NET_DEVICE_ID);
    else if (error!=ESP_OK) goto done;
    if (bytes==17&&!memcmp(line,"PET_GROWTH_EXPORT",17)) {
        uint8_t data[PET_GROWTH_BACKUP_MAX];size_t length;uint32_t revision;
        result=pet_growth_export(data,sizeof(data),&length,&revision);
        if (result==PET_GROWTH_OK) {
            char hex[PET_GROWTH_BACKUP_MAX*2+1];
            for (size_t i=0;i<length;++i) snprintf(hex+i*2,3,"%02x",data[i]);
            cJSON *root=cJSON_CreateObject(),*backup=cJSON_AddObjectToObject(root,"backup");
            cJSON_AddBoolToObject(root,"ok",true);cJSON_AddStringToObject(backup,"deviceId",device);
            cJSON_AddNumberToObject(backup,"format",1);cJSON_AddNumberToObject(backup,"revision",revision);
            cJSON_AddStringToObject(backup,"payload",hex);
            bool ok=cJSON_PrintPreallocated(root,out,(int)cap,false);cJSON_Delete(root);
            if (ok) return ESP_OK;
            result=PET_GROWTH_INVALID;
        }
    } else if (bytes>19&&!memcmp(line,"PET_GROWTH_RESTORE ",19)) {
        const char *end=NULL;const char *json=line+19;size_t size=bytes-19;
        /* Flat envelope has no nested data; reject nesting before cJSON recursion. */
        bool quoted=false,escaped=false;unsigned depth=0;bool bounded=true;
        for (size_t i=0;i<size;++i) {char c=json[i];if (quoted) {if (escaped) escaped=false;else if(c=='\\') escaped=true;else if(c=='"') quoted=false;}else if(c=='"')quoted=true;else if(c=='{'||c=='['){if(++depth>1)bounded=false;}else if(c=='}'||c==']'){if(!depth)bounded=false;else --depth;}}
        cJSON *root=bounded?cJSON_ParseWithLengthOpts(json,size,&end,false):NULL;
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"deviceId"),*format=cJSON_GetObjectItemCaseSensitive(root,"format"),
            *expected=cJSON_GetObjectItemCaseSensitive(root,"expectedRevision"),*payload=cJSON_GetObjectItemCaseSensitive(root,"payload");
        if (cJSON_IsObject(root)&&end==line+bytes&&cJSON_GetArraySize(root)==4&&cJSON_IsString(id)&&!strcmp(id->valuestring,device)&&
            cJSON_IsNumber(format)&&format->valuedouble==1&&cJSON_IsNumber(expected)&&expected->valuedouble>=1&&
            expected->valuedouble<=UINT32_MAX&&floor(expected->valuedouble)==expected->valuedouble&&cJSON_IsString(payload)) {
            size_t n=strlen(payload->valuestring);uint8_t data[PET_GROWTH_BACKUP_MAX];bool valid=n%2==0&&n<=sizeof(data)*2;
            for (size_t i=0;valid&&i<n;i+=2) {int a=nibble(payload->valuestring[i]),b=nibble(payload->valuestring[i+1]);if(a<0||b<0)valid=false;else data[i/2]=(uint8_t)(a*16+b);}
            if (valid) result=pet_growth_import(data,n/2,(uint32_t)expected->valuedouble);
        }
        cJSON_Delete(root);
    }
done:
    snprintf(out,cap,"{\"ok\":%s,\"code\":%d}",result==PET_GROWTH_OK?"true":"false",result);
    return result==PET_GROWTH_OK?ESP_OK:ESP_FAIL;
}
