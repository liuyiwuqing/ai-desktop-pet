#include "sdkconfig.h"
#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/control_service.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdatomic.h>
#include "cJSON.h"
#include "pet/json_guard.h"
#include "pet/nexus.h"
#include "pet/storage.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
typedef struct {unsigned kind;pet_command_request_t request;uint64_t nonce;bool approved;} job_t;
static struct {
    char device[41],uri[256],token[129],ca[3001],apps[656],projects[656];
    const char *app_ids[16],*project_ids[16];
    pet_command_capability_t capabilities[3];
    pet_command_engine_t engine;
    uint64_t watermark;
    bool storage_uncertain;
} s;
static QueueHandle_t queue;
static portMUX_TYPE view_lock=portMUX_INITIALIZER_UNLOCKED;
static pet_control_view_t view;
static _Atomic bool deciding;
static uint64_t last_job_id;
static pet_command_status_t last_job_status;
static uint64_t clock_ms(void *ctx){(void)ctx;return (uint64_t)esp_timer_get_time()/1000;}
static uint64_t nonce(void *ctx){(void)ctx;uint64_t value;esp_fill_random(&value,sizeof(value));return value;}
static bool reserve(void *ctx,uint64_t id)
{
    (void)ctx;
    if(s.storage_uncertain||id<=s.watermark)return false;
    uint8_t value[8],check[8];size_t bytes=0;
    for(unsigned i=0;i<8;++i)value[i]=(uint8_t)(id>>(i*8));
    (void)pet_storage_save("pet_control","watermark",value,sizeof(value));
    if(pet_storage_load("pet_control","watermark",check,sizeof(check),&bytes)!=ESP_OK||bytes!=8||memcmp(value,check,8)) {
        s.storage_uncertain=true;return false;
    }
    s.watermark=id;return true;
}
static bool within(esp_http_client_handle_t client,uint64_t deadline)
{
    uint64_t now=clock_ms(NULL);if(now>=deadline)return false;
    return esp_http_client_set_timeout_ms(client,(int)(deadline-now))==ESP_OK;
}
static pet_command_status_t http(const pet_command_request_t *request,bool lookup,uint32_t timeout,pet_command_result_t *out)
{
    char url[384],authorization[136],body[512];char response[768];size_t used=0;
    if(lookup)snprintf(url,sizeof(url),"%s/v1/results/%s/%llu",s.uri,s.device,(unsigned long long)request->id);
    else snprintf(url,sizeof(url),"%s/v1/execute",s.uri);
    snprintf(authorization,sizeof(authorization),"Bearer %s",s.token);
    uint32_t epoch=(uint32_t)time(NULL);
    if(epoch<1577836800||epoch>UINT32_MAX-30)return PET_COMMAND_UNAVAILABLE;
    if(!lookup) {
        /* L0 也占用水位：代理和 bridge 的请求键在重启后保持唯一。 */
        if(request->id>s.watermark&&!reserve(NULL,request->id))return PET_COMMAND_UNAVAILABLE;
        cJSON *root=cJSON_CreateObject(),*args=cJSON_AddObjectToObject(root,"arguments");
        cJSON_AddStringToObject(root,"deviceId",s.device);cJSON_AddNumberToObject(root,"requestId",(double)request->id);
        cJSON_AddStringToObject(root,"toolId",request->capability);cJSON_AddBoolToObject(root,"approved",!strcmp(request->capability,"app.open"));
        cJSON_AddNumberToObject(root,"expiresAt",epoch+30);
        if(!strcmp(request->capability,"app.open"))cJSON_AddStringToObject(args,"applicationId",request->argument);
        else if(!strcmp(request->capability,"project.status"))cJSON_AddStringToObject(args,"projectId",request->argument);
        bool encoded=cJSON_PrintPreallocated(root,body,sizeof(body),false);cJSON_Delete(root);
        if(!encoded)return PET_COMMAND_FAILED;
    }
    const esp_http_client_config_t config={.url=url,.timeout_ms=(int)timeout,.disable_auto_redirect=true,
        .cert_pem=s.ca[0]?s.ca:NULL,.crt_bundle_attach=s.ca[0]?NULL:esp_crt_bundle_attach,.buffer_size=512};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    if(!client)return PET_COMMAND_UNAVAILABLE;
    esp_http_client_set_header(client,"Authorization",authorization);
    esp_http_client_set_header(client,"Content-Type","application/json");
    esp_http_client_set_method(client,lookup?HTTP_METHOD_GET:HTTP_METHOD_POST);
    uint64_t deadline=clock_ms(NULL)+timeout;
    int bytes=lookup?0:(int)strlen(body);pet_command_status_t status=PET_COMMAND_UNKNOWN;
    if(!within(client,deadline)||esp_http_client_open(client,bytes)!=ESP_OK)goto done;
    if(bytes&&(!within(client,deadline)||esp_http_client_write(client,body,bytes)!=bytes))goto done;
    if(!within(client,deadline)||esp_http_client_fetch_headers(client)<0)goto done;
    int code=esp_http_client_get_status_code(client);
    if(code!=200){status=code>=400&&code<500?PET_COMMAND_DENIED:PET_COMMAND_UNKNOWN;goto done;}
    for(;;) {
        if(used==sizeof(response)-1||!within(client,deadline))goto done;
        int read=esp_http_client_read(client,response+used,(int)(sizeof(response)-1-used));
        if(read<0)goto done;
        if(!read)break;
        used+=(size_t)read;
    }
    if(!esp_http_client_is_complete_data_received(client)||!pet_json_bounded(response,used,3))goto done;
    response[used]=0;cJSON *root=cJSON_Parse(response);
    const cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"requestId"),*state=cJSON_GetObjectItemCaseSensitive(root,"state"),
        *summary=cJSON_GetObjectItemCaseSensitive(root,"summary");
    if(cJSON_IsNumber(id)&&id->valuedouble==(double)request->id&&cJSON_IsString(state)) {
        status=!strcmp(state->valuestring,"succeeded")?PET_COMMAND_OK:!strcmp(state->valuestring,"failed")?PET_COMMAND_FAILED:PET_COMMAND_UNKNOWN;
        if(cJSON_IsString(summary))snprintf(out->summary,sizeof(out->summary),"%.120s",summary->valuestring);
    }
    cJSON_Delete(root);
done:
    esp_http_client_close(client);esp_http_client_cleanup(client);
    memset(authorization,0,sizeof(authorization));return status;
}
static pet_command_status_t execute(void *ctx,const pet_command_request_t *request,uint32_t timeout,pet_command_result_t *out)
{(void)ctx;return http(request,false,timeout,out);}
static void publish(void)
{
    uint64_t token=0;pet_command_request_t request={0};
    bool pending=pet_command_pending(&s.engine,&request,&token)&&!atomic_load(&deciding);
    pet_control_view_t next={.configured=true,.awaiting_confirmation=pending,
        .next_request_id=s.engine.highest_id+1,.confirmation_nonce=token,.expires_ms=s.engine.expires_ms,
        .request=s.engine.request,.result=s.engine.result,.last_job_id=last_job_id,.last_job_status=last_job_status};
    portENTER_CRITICAL(&view_lock);view=next;portEXIT_CRITICAL(&view_lock);
}
static void owner(void *ctx)
{
    (void)ctx;
    for(;;) {
        job_t job;
        if(xQueueReceive(queue,&job,pdMS_TO_TICKS(20))==pdTRUE) {
            pet_command_result_t result={.status=PET_COMMAND_DENIED};
            if(job.kind==0)(void)pet_command_submit(&s.engine,&job.request,&result);
            else if(job.kind==1) {
                (void)pet_command_confirm(&s.engine,job.request.id,job.nonce,job.approved,&result);
                atomic_store(&deciding,false);
            } else if(job.request.id<=s.engine.highest_id) {
                result.status=http(&job.request,true,4000,&result);
                if(job.request.id==s.engine.request.id)s.engine.result=result;
            }
            last_job_id=job.request.id;last_job_status=result.status;
        }
        publish();
    }
}
static size_t split(char *text,const char **out)
{
    if(!text[0])return 0;
    size_t count=0;char *p=text;
    for(;;) {
        if(count==16||!p[0])return 17;
        out[count++]=p;char *comma=strchr(p,',');if(!comma)break;*comma=0;p=comma+1;
    }
    return count;
}
esp_err_t pet_control_service_start(const char *device,esp_err_t (*load)(const char *,const char *,char *,size_t))
{
    if(queue||!load||!pet_nexus_device_id_valid(device))return ESP_ERR_INVALID_ARG;
    memset(&s,0,sizeof(s));snprintf(s.device,sizeof(s.device),"%s",device);
    esp_err_t error=load("control","api_uri",s.uri,sizeof(s.uri));if(error!=ESP_OK)return error;
    error=load("control","token",s.token,sizeof(s.token));if(error!=ESP_OK)return error;
    const struct {const char *key;char *out;size_t cap;} optional[]={
        {"ca",s.ca,sizeof(s.ca)},{"allowed_apps",s.apps,sizeof(s.apps)},{"allowed_projects",s.projects,sizeof(s.projects)}};
    for(size_t i=0;i<3;++i){error=load("control",optional[i].key,optional[i].out,optional[i].cap);if(error!=ESP_OK&&error!=ESP_ERR_NVS_NOT_FOUND)return error;}
    size_t uri_length=strlen(s.uri);while(uri_length&&s.uri[uri_length-1]=='/')s.uri[--uri_length]=0;
    if(strncmp(s.uri,"https://",8)||!s.uri[8]||strpbrk(s.uri+8,"@?# \r\n")||strlen(s.token)<32)return ESP_ERR_INVALID_ARG;
    for(const char *p=s.token;*p;++p)if((unsigned char)*p<=32||*p==127)return ESP_ERR_INVALID_ARG;
    size_t apps=split(s.apps,s.app_ids),projects=split(s.projects,s.project_ids);if(apps>16||projects>16)return ESP_ERR_INVALID_ARG;
    s.capabilities[0]=(pet_command_capability_t){"mac","system.info",PET_EXT_RISK_L0_READ,false,NULL,0,4000};
    size_t count=1;
    if(apps)s.capabilities[count++]=(pet_command_capability_t){"mac","app.open",PET_EXT_RISK_L1_REVERSIBLE,true,s.app_ids,apps,4000};
    if(projects)s.capabilities[count++]=(pet_command_capability_t){"mac","project.status",PET_EXT_RISK_L0_READ,false,s.project_ids,projects,4000};
    uint8_t bytes[8];size_t length=0;error=pet_storage_init();if(error!=ESP_OK)return error;
    error=pet_storage_load("pet_control","watermark",bytes,sizeof(bytes),&length);
    if(error==ESP_OK){if(length!=8)return ESP_ERR_INVALID_STATE;for(unsigned i=0;i<8;++i)s.watermark|=(uint64_t)bytes[i]<<(i*8);}
    else if(error!=ESP_ERR_NVS_NOT_FOUND)return error;
    pet_command_adapter_t adapter={NULL,execute,clock_ms,nonce,reserve};
    if(!pet_command_init(&s.engine,s.capabilities,count,&adapter,s.watermark))return ESP_ERR_INVALID_ARG;
    queue=xQueueCreate(4,sizeof(job_t));if(!queue)return ESP_ERR_NO_MEM;
    publish();
    if(xTaskCreate(owner,"pet_control",8192,NULL,3,NULL)!=pdPASS){vQueueDelete(queue);queue=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t pet_control_service_submit(const pet_command_request_t *request)
{
    if(!queue)return ESP_ERR_INVALID_STATE;
    if(!request||!request->id||request->id>9007199254740991ULL)return ESP_ERR_INVALID_ARG;
    const job_t job={.request=*request};return xQueueSend(queue,&job,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t pet_control_service_confirm(uint64_t id,uint64_t token,bool approved)
{
    if(!queue)return ESP_ERR_INVALID_STATE;
    bool expected=false;if(!atomic_compare_exchange_strong(&deciding,&expected,true))return ESP_ERR_TIMEOUT;
    const job_t job={.kind=1,.request={.id=id},.nonce=token,.approved=approved};
    if(xQueueSend(queue,&job,0)!=pdTRUE){atomic_store(&deciding,false);return ESP_ERR_TIMEOUT;}
    return ESP_OK;
}
esp_err_t pet_control_service_reconcile(uint64_t id)
{
    if(!queue)return ESP_ERR_INVALID_STATE;
    const job_t job={.kind=2,.request={.id=id}};return id&&xQueueSend(queue,&job,0)==pdTRUE?ESP_OK:ESP_ERR_INVALID_ARG;
}
esp_err_t pet_control_service_snapshot(pet_control_view_t *out)
{if(!out)return ESP_ERR_INVALID_ARG;portENTER_CRITICAL(&view_lock);*out=view;portEXIT_CRITICAL(&view_lock);return queue?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t pet_control_service_catalog(pet_control_catalog_t *out)
{
    if(!out)return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));if(!queue)return ESP_ERR_INVALID_STATE;
    for(size_t i=0;i<16&&s.app_ids[i];++i){snprintf(out->applications[i],41,"%s",s.app_ids[i]);++out->application_count;}
    for(size_t i=0;i<16&&s.project_ids[i];++i){snprintf(out->projects[i],41,"%s",s.project_ids[i]);++out->project_count;}
    return ESP_OK;
}
#endif
