#include "sdkconfig.h"
#include "app_growth_sync.h"
#if CONFIG_PET_GROWTH_SYNC_ENABLED
#include "svc_growth/api.h"
#include "svc_growth/sync.h"
#include "svc_growth/companion_sync.h"
#include "esp_timer.h"
#include "svc_net/api.h"
#include "esp_random.h"
#include <stdio.h>
#include <string.h>

static char device_id[PET_NET_DEVICE_ID_MAX+1],boot_id[33];
static uint32_t sequence,last_report;
void app_growth_sync_init(const char *device)
{
    if(device_id[0]||!pet_nexus_device_id_valid(device))return;
    (void)snprintf(device_id,sizeof(device_id),"%s",device);
    unsigned char random[16];esp_fill_random(random,sizeof(random));
    for(unsigned i=0;i<16;i++)(void)snprintf(boot_id+i*2,3,"%02x",random[i]);
}
void app_growth_sync_poll(uint32_t epoch)
{
    if(!device_id[0]||!epoch||sequence==UINT32_MAX||!pet_net_is_online()||
        (last_report&&epoch>=last_report&&epoch-last_report<15))return;
    pet_growth_view_t snapshot;
    if(pet_growth_get_snapshot(&snapshot)!=PET_GROWTH_OK)return;
    char report[PET_COMPANION_SYNC_MAX];
    int bytes=pet_growth_sync_format(device_id,boot_id,++sequence,epoch,&snapshot,report,sizeof(report));
    if(bytes>0&&pet_net_publish_topic(PET_NET_TOPIC_GROWTH_STATUS,report,(size_t)bytes,false)==ESP_OK)last_report=epoch;
#if CONFIG_PET_COMPANION_SYNC_ENABLED
    pet_companion_view_t companion;
    if(pet_companion_snapshot(&companion)==PET_GROWTH_OK) {
        bytes=pet_companion_sync_format(device_id,boot_id,sequence,epoch,snapshot.revision,&companion,report,sizeof(report));
        if(bytes>0)(void)pet_net_publish_topic(PET_NET_TOPIC_COMPANION_STATUS,report,(size_t)bytes,false);
    }
#endif
}
#else
void app_growth_sync_init(const char *device){(void)device;}
void app_growth_sync_poll(uint32_t epoch){(void)epoch;}
#endif

bool app_companion_message(const char *topic,const char *payload,size_t bytes,bool retained,uint32_t epoch)
{
#if CONFIG_PET_COMPANION_SYNC_ENABLED
    char expected[PET_NET_TOPIC_MAX];
    int length=snprintf(expected,sizeof(expected),"nexus/v1/devices/%s/appearance",device_id);
    if(!device_id[0]||!topic||length<0||(size_t)length>=sizeof(expected)||strcmp(topic,expected))return false;
    char reply[256];
    (void)pet_companion_remote(device_id,payload,bytes,retained,epoch,(uint64_t)esp_timer_get_time()/1000,reply,sizeof(reply));
    (void)pet_net_publish_confirmed(PET_NET_TOPIC_APPEARANCE_ACK,reply,strlen(reply),false,3000);
    return true;
#else
    (void)topic;(void)payload;(void)bytes;(void)retained;(void)epoch;return false;
#endif
}
