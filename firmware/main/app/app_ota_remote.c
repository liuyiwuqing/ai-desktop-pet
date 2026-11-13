#include "sdkconfig.h"
#include "app_ota_remote.h"
#if CONFIG_PET_OTA_REMOTE_ENABLED
#include "app_recovery.h"
#include "svc_ota/remote.h"
#include "svc_config/api.h"
#include "svc_ai_agent/api.h"
#include "svc_net/api.h"
#include "pet/storage.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
static pet_ota_remote_t *service;
static SemaphoreHandle_t mutex;
static char command_topic[PET_NET_TOPIC_MAX];
static uint32_t last_report;
static esp_err_t load(void *ctx,void *out,size_t capacity,size_t *bytes)
{(void)ctx;return pet_storage_load("pet_ota","remote_v1",out,capacity,bytes);}
static esp_err_t save(void *ctx,const void *data,size_t bytes)
{(void)ctx;return pet_storage_save("pet_ota","remote_v1",data,bytes);}
static esp_err_t apply(void *ctx,const pet_ota_target_t *target)
{(void)ctx;return pet_ota_apply_target(target);}
static esp_err_t reboot(void *ctx){(void)ctx;return pet_ota_reboot();}
static esp_err_t snapshot(void *ctx,pet_ota_snapshot_t *out){(void)ctx;return pet_ota_snapshot(out);}
static pet_ota_image_state_t running(void *ctx,const pet_ota_target_t *target)
{(void)ctx;return pet_ota_running_target(target);}
static bool allowed(void *ctx)
{
    (void)ctx;if(app_recovery_active())return false;
    pet_config_snapshot_t settings;
    if(pet_config_snapshot(&settings)!=ESP_OK||!settings.writable)return false;
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
    pet_ai_agent_snapshot_t ai;esp_err_t error=pet_ai_agent_snapshot(&ai);
    if(error==ESP_OK&&(!ai.ready||ai.faulted||ai.capture_active||ai.playback_active))return false;
    if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return false;
#endif
    return true;
}
void app_ota_remote_init(const char *device)
{
    if(mutex||!pet_nexus_device_id_valid(device))return;
    mutex=xSemaphoreCreateMutex();if(!mutex)return;
    unsigned char random[16];char boot[33];esp_fill_random(random,sizeof(random));
    for(unsigned i=0;i<16;++i)snprintf(boot+i*2,3,"%02x",random[i]);
    const pet_ota_remote_port_t port={NULL,load,save,apply,reboot,snapshot,running,allowed};
    (void)pet_ota_remote_init(&service,device,boot,pet_ota_identity(),&port);
    (void)snprintf(command_topic,sizeof(command_topic),PET_NEXUS_TOPIC_PREFIX "%s/ota",device);
}
bool app_ota_remote_message(const char *topic,const char *payload,size_t bytes,bool retained,uint32_t now)
{
    if(!service||strcmp(topic,command_topic))return false;
    char reply[512];xSemaphoreTake(mutex,portMAX_DELAY);
    (void)pet_ota_remote_command(service,payload,bytes,retained,now,reply,sizeof(reply));
    last_report=0;xSemaphoreGive(mutex);
    (void)pet_net_publish_confirmed(PET_NET_TOPIC_OTA_ACK,reply,strlen(reply),false,3000);
    return true;
}
void app_ota_remote_poll(uint32_t now)
{
    if(!service||!now||!pet_net_is_online()||xSemaphoreTake(mutex,0)!=pdTRUE)return;
    (void)pet_ota_remote_tick(service);
    if(!last_report||now<last_report||now-last_report>=5) {
        char report[1536];
        if(pet_ota_remote_status(service,now,report,sizeof(report))==ESP_OK&&
            pet_net_publish_topic(PET_NET_TOPIC_OTA_STATUS,report,strlen(report),false)==ESP_OK)last_report=now;
    }
    xSemaphoreGive(mutex);
}
#else
void app_ota_remote_init(const char *device){(void)device;}
bool app_ota_remote_message(const char *topic,const char *payload,size_t bytes,bool retained,uint32_t now)
{(void)topic;(void)payload;(void)bytes;(void)retained;(void)now;return false;}
void app_ota_remote_poll(uint32_t now){(void)now;}
#endif
