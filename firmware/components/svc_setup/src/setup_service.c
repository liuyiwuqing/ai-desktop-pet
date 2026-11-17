#include "setup_internal.h"
#include "setup_policy.h"
#include "svc_config/provision.h"
#include "svc_net/netif_ip.h"
#include "cJSON.h"
#include "pet/json_guard.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <stdio.h>
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
    char value[65]={0};uint32_t revision=0;
    esp_err_t err=pet_provision_revision(&revision);
    bool wifi=pet_provision_load_str("net","wifi_ssid",value,sizeof(value))==ESP_OK&&value[0];
    bool voice=pet_provision_load_str("xiaozhi","device_id",value,sizeof(value))==ESP_OK&&value[0];
    bool nexus=pet_provision_load_str("nexus","device_id",value,sizeof(value))==ESP_OK&&value[0];
    xSemaphoreTake(s->lock,portMAX_DELAY);
    if(err==ESP_OK) s->snapshot.revision=revision;
    s->snapshot.wifi_saved=wifi;s->snapshot.voice_saved=voice;s->snapshot.nexus_saved=nexus;
    if(wifi) snprintf(s->snapshot.wifi_ssid,sizeof(s->snapshot.wifi_ssid),"%s",value);
    else s->snapshot.wifi_ssid[0]=0;
    xSemaphoreGive(s->lock);
    setup_wipe(value,sizeof(value));
}

/* 地址设置直接来自 net 凭据组；缺键即默认 DHCP，坏值回落默认以免卡住配网。 */
static void load_netcfg(pet_setup_netcfg_t *out)
{
    pet_setup_netcfg_default(out);
    char mode[8]={0};
    if(pet_provision_load_str("net","ip_mode",mode,sizeof(mode))==ESP_OK) {
        snprintf(out->ip_mode,sizeof(out->ip_mode),"%s",mode);
    }
    setup_wipe(mode,sizeof(mode));
    struct { const char *key; char *value; size_t capacity; } fields[] = {
        {"ip",out->ip,sizeof(out->ip)},{"netmask",out->netmask,sizeof(out->netmask)},
        {"gateway",out->gateway,sizeof(out->gateway)},{"dns",out->dns,sizeof(out->dns)},
        {"hostname",out->hostname,sizeof(out->hostname)},
    };
    for(unsigned i=0;i<sizeof(fields)/sizeof(fields[0]);i++)
        if(pet_provision_load_str("net",fields[i].key,fields[i].value,fields[i].capacity)!=ESP_OK)
            fields[i].value[0]=0;
    if(!pet_setup_netcfg_valid(out)) pet_setup_netcfg_default(out);
}

static esp_err_t save_network(setup_runtime_t *s,const pet_setup_netcfg_t *netcfg,uint32_t revision)
{
    char ssid[33]={0},password[65]={0};
    esp_err_t err=pet_provision_load_str("net","wifi_ssid",ssid,sizeof(ssid));
    if(err!=ESP_OK||!ssid[0]) {err=ESP_ERR_INVALID_STATE;goto done;}
    if(pet_provision_load_str("net","wifi_pass",password,sizeof(password))!=ESP_OK) password[0]=0;
    cJSON *root=cJSON_CreateObject(),*net=root?cJSON_AddObjectToObject(root,"net"):NULL;
    char *encoded=NULL;err=ESP_ERR_NO_MEM;
    if(!net) goto built;
    if(!cJSON_AddStringToObject(net,"wifi_ssid",ssid)) goto built;
    if(!cJSON_AddStringToObject(net,"wifi_pass",password)) goto built;
    if(!cJSON_AddStringToObject(net,"ip_mode",netcfg->ip_mode)) goto built;
    if(pet_netcfg_static(netcfg->ip_mode)) {
        if(!cJSON_AddStringToObject(net,"ip",netcfg->ip)) goto built;
        if(!cJSON_AddStringToObject(net,"netmask",netcfg->netmask)) goto built;
        if(!cJSON_AddStringToObject(net,"gateway",netcfg->gateway)) goto built;
        if(netcfg->dns[0]&&!cJSON_AddStringToObject(net,"dns",netcfg->dns)) goto built;
    }
    if(netcfg->hostname[0]&&!cJSON_AddStringToObject(net,"hostname",netcfg->hostname)) goto built;
    encoded=cJSON_PrintUnformatted(root);
    if(encoded) {err=pet_provision_patch(encoded,strlen(encoded),revision);setup_wipe(encoded,strlen(encoded));cJSON_free(encoded);}
built:
    cJSON_Delete(root);
    if(err==ESP_OK) {
        /* 已保存即在本轮生效：下一次“连接并保存”用的就是新地址。 */
        esp_err_t applied=pet_netif_apply_ip(s->sta,netcfg->ip_mode,netcfg->ip,netcfg->netmask,
            netcfg->gateway,netcfg->dns,netcfg->hostname);
        if(applied!=ESP_OK) err=applied;
    }
done:
    setup_wipe(ssid,sizeof(ssid));setup_wipe(password,sizeof(password));
    return err;
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
        } else if(job.kind==SETUP_JOB_NETWORK) {
            err=save_network(s,&job.netcfg,job.revision);
            if(err==ESP_OK) {
                xSemaphoreTake(s->lock,portMAX_DELAY);
                s->netcfg=job.netcfg;s->snapshot.netcfg=job.netcfg;
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
            kind==SETUP_JOB_PORTAL?PET_SETUP_PORTAL_SAVED:
            kind==SETUP_JOB_NETWORK?PET_SETUP_NETWORK_SAVED:PET_SETUP_SERVICES_SAVED:
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

esp_err_t pet_setup_submit_network(const pet_setup_netcfg_t *netcfg,uint32_t revision)
{
    if(!pet_setup_netcfg_valid(netcfg)) return ESP_ERR_INVALID_ARG;
    setup_job_t job={.kind=SETUP_JOB_NETWORK,.revision=revision};
    memcpy(&job.netcfg,netcfg,sizeof(job.netcfg));
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
    load_netcfg(&s->netcfg);
    s->snapshot.netcfg=s->netcfg;
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
