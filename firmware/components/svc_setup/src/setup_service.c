#include "setup_internal.h"
#include "setup_policy.h"
#include "svc_config/provision.h"
#include "cJSON.h"
#include "pet/json_guard.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <string.h>

static setup_runtime_t *runtime;

void setup_wipe(void *memory,size_t bytes)
{
    volatile unsigned char *p=memory;
    while(bytes--) *p++=0;
}

void setup_state(setup_runtime_t *s,pet_setup_state_t state,esp_err_t error)
{
    xSemaphoreTake(s->lock,portMAX_DELAY);
    s->snapshot.state=state;s->snapshot.error=error;
    xSemaphoreGive(s->lock);
}

bool setup_cancelled(setup_runtime_t *s)
{
    return atomic_load(&s->leaving)||esp_timer_get_time()>=s->deadline_us;
}

esp_err_t pet_setup_snapshot(pet_setup_snapshot_t *out)
{
    if(!runtime) return ESP_ERR_INVALID_STATE;
    if(!out) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(runtime->lock,portMAX_DELAY);
    *out=runtime->snapshot;
    int64_t remaining=runtime->deadline_us-esp_timer_get_time();
    out->remaining_s=remaining>0?(uint32_t)(remaining/1000000):0;
    xSemaphoreGive(runtime->lock);return ESP_OK;
}

static void refresh_saved(setup_runtime_t *s)
{
    char value[65];uint32_t revision=0;
    esp_err_t err=pet_provision_revision(&revision);
    bool wifi=pet_provision_load_str("net","wifi_ssid",value,sizeof(value))==ESP_OK&&value[0];
    bool voice=pet_provision_load_str("xiaozhi","device_id",value,sizeof(value))==ESP_OK&&value[0];
    bool nexus=pet_provision_load_str("nexus","device_id",value,sizeof(value))==ESP_OK&&value[0];
    setup_wipe(value,sizeof(value));
    xSemaphoreTake(s->lock,portMAX_DELAY);
    if(err==ESP_OK) s->snapshot.revision=revision;
    s->snapshot.wifi_saved=wifi;s->snapshot.voice_saved=voice;s->snapshot.nexus_saved=nexus;
    xSemaphoreGive(s->lock);
}

static esp_err_t save_wifi(setup_job_t *job)
{
    cJSON *root=cJSON_CreateObject(),*net=root?cJSON_AddObjectToObject(root,"net"):NULL;
    char *encoded=NULL;esp_err_t err=ESP_ERR_NO_MEM;
    if(net&&cJSON_AddStringToObject(net,"wifi_ssid",job->ssid)&&cJSON_AddStringToObject(net,"wifi_pass",job->password))
        encoded=cJSON_PrintUnformatted(root);
    if(encoded) {err=pet_provision_patch(encoded,strlen(encoded),job->revision);setup_wipe(encoded,strlen(encoded));cJSON_free(encoded);}
    cJSON *password=cJSON_GetObjectItemCaseSensitive(net,"wifi_pass");
    if(cJSON_IsString(password)) setup_wipe(password->valuestring,strlen(password->valuestring));
    cJSON_Delete(root);return err;
}

static void setup_task(void *context)
{
    setup_runtime_t *s=context;
    setup_state(s,PET_SETUP_READY,ESP_OK);
    while(!setup_cancelled(s)) {
        setup_job_t job={0};
        if(xQueueReceive(s->jobs,&job,pdMS_TO_TICKS(100))!=pdTRUE) continue;
        esp_err_t err=ESP_ERR_INVALID_ARG;
        const setup_job_kind_t kind=job.kind;
        const bool restart=job.restart;
        if(job.kind==SETUP_JOB_WIFI) {
            setup_state(s,PET_SETUP_CONNECTING,ESP_OK);
            err=setup_network_connect(s,job.ssid,job.password);
            if(err==ESP_OK&&!setup_cancelled(s)) err=save_wifi(&job);
        } else if(job.kind==SETUP_JOB_SERVICES) {
            err=pet_provision_patch(job.json,job.bytes,job.revision);
        } else if(job.kind==SETUP_JOB_PORTAL) {
            err=setup_portal_save(&job.portal);
            if(err==ESP_OK) {
                xSemaphoreTake(s->lock,portMAX_DELAY);
                s->portal=job.portal;s->snapshot.portal=job.portal;
                xSemaphoreGive(s->lock);
            }
        } else if(job.kind==SETUP_JOB_ACTIVATE) {
            setup_state(s,PET_SETUP_ACTIVATING,ESP_OK);
            err=setup_activation_run(s);
        }
        if(job.json) {setup_wipe(job.json,job.bytes);heap_caps_free(job.json);}
        setup_wipe(&job,sizeof(job));
        refresh_saved(s);
        if(!setup_cancelled(s)) setup_state(s,err==ESP_OK?
            kind==SETUP_JOB_ACTIVATE?PET_SETUP_VOICE_SAVED:kind==SETUP_JOB_WIFI?PET_SETUP_WIFI_SAVED:
            kind==SETUP_JOB_PORTAL?PET_SETUP_PORTAL_SAVED:PET_SETUP_SERVICES_SAVED:
            PET_SETUP_FAILED,err);
        atomic_store(&s->busy,false);
        /* 热点偏好不影响当前连接；用户要求立即生效时以设置模式重启，等响应发出再复位。 */
        if(kind==SETUP_JOB_PORTAL&&restart&&err==ESP_OK&&setup_boot_save(1)==ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(800));
            esp_restart();
        }
    }
    setup_state(s,PET_SETUP_LEAVING,ESP_OK);
    /* 关闭门户仅保存进入模式，不删除任何 Wi-Fi、服务凭据或成长存档。 */
    if(setup_boot_save(2)==ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(600));
        esp_restart();
    }
    if(s->http) {httpd_stop(s->http);s->http=NULL;}
    esp_wifi_stop();
    setup_state(s,PET_SETUP_FAILED,ESP_FAIL);
    vTaskDelete(NULL);
}

static esp_err_t submit(setup_job_t *job)
{
    setup_runtime_t *s=runtime;
    if(!s||setup_cancelled(s)) return ESP_ERR_INVALID_STATE;
    if(atomic_exchange(&s->busy,true)) return ESP_ERR_TIMEOUT;
    if(xQueueSend(s->jobs,job,0)!=pdTRUE) {atomic_store(&s->busy,false);return ESP_ERR_TIMEOUT;}
    return ESP_OK;
}

esp_err_t pet_setup_submit_wifi(const char *ssid,const char *password,uint32_t revision)
{
    if(!pet_setup_wifi_valid(ssid,password)) return ESP_ERR_INVALID_ARG;
    setup_job_t job={.kind=SETUP_JOB_WIFI,.revision=revision};
    memcpy(job.ssid,ssid,strlen(ssid));memcpy(job.password,password,strlen(password));
    esp_err_t err=submit(&job);setup_wipe(&job,sizeof(job));return err;
}

esp_err_t pet_setup_submit_services(const char *json,size_t bytes,uint32_t revision)
{
    if(!json||!bytes||bytes>PET_PROVISION_JSON_MAX||!pet_json_bounded(json,bytes,4)) return ESP_ERR_INVALID_ARG;
    setup_job_t job={.kind=SETUP_JOB_SERVICES,.revision=revision,.bytes=bytes};
    job.json=heap_caps_malloc(bytes+1,MALLOC_CAP_8BIT);
    if(!job.json) return ESP_ERR_NO_MEM;
    memcpy(job.json,json,bytes);job.json[bytes]=0;
    esp_err_t err=submit(&job);
    if(err!=ESP_OK) {setup_wipe(job.json,bytes+1);heap_caps_free(job.json);}
    return err;
}

esp_err_t pet_setup_activate(void)
{
    setup_job_t job={.kind=SETUP_JOB_ACTIVATE};
    return submit(&job);
}

esp_err_t pet_setup_submit_portal(const pet_setup_portal_t *portal,bool restart,uint32_t revision)
{
    if(!pet_setup_portal_valid(portal)) return ESP_ERR_INVALID_ARG;
    setup_job_t job={.kind=SETUP_JOB_PORTAL,.revision=revision,.restart=restart,.portal=*portal};
    esp_err_t err=submit(&job);setup_wipe(&job,sizeof(job));return err;
}

esp_err_t pet_setup_leave(void)
{
    if(!runtime) return ESP_ERR_INVALID_STATE;
    atomic_store(&runtime->leaving,true);return ESP_OK;
}

esp_err_t pet_setup_start(void)
{
    if(runtime) return ESP_ERR_INVALID_STATE;
    uint32_t revision;esp_err_t err=pet_provision_revision(&revision);
    if(err!=ESP_OK) return err;
    setup_runtime_t *s=heap_caps_calloc(1,sizeof(*s),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!s) return ESP_ERR_NO_MEM;
    s->lock=xSemaphoreCreateMutex();s->jobs=xQueueCreate(1,sizeof(setup_job_t));s->network_events=xEventGroupCreate();
    if(!s->lock||!s->jobs||!s->network_events) {
        if(s->jobs) vQueueDelete(s->jobs);
        if(s->network_events) vEventGroupDelete(s->network_events);
        if(s->lock) vSemaphoreDelete(s->lock);
        heap_caps_free(s);return ESP_ERR_NO_MEM;
    }
    /* 损坏或不可读的偏好回落默认热点；不阻断配网，也不改动已保存的家庭凭据。 */
    (void)setup_portal_load(&s->portal);
    s->snapshot.portal=s->portal;
    s->deadline_us=esp_timer_get_time()+(int64_t)s->portal.window_s*1000000;
    refresh_saved(s);
    err=setup_network_start(s);
    if(err==ESP_OK) err=setup_http_start(s);
    if(err!=ESP_OK) goto fail;
    runtime=s;
    if(xTaskCreate(setup_task,"pet_setup",8192,s,3,NULL)!=pdPASS) {runtime=NULL;err=ESP_ERR_NO_MEM;goto fail;}
    return ESP_OK;
fail:
    if(s->http) httpd_stop(s->http);
    /* 不释放仍被 Wi-Fi 回调引用的 context；失败由组合根进入维护并重启恢复。 */
    runtime=s;setup_state(s,PET_SETUP_FAILED,err);return err;
}
