#include "sdkconfig.h"
#include "app_growth_sync.h"
#if CONFIG_PET_GROWTH_SYNC_ENABLED
#include "svc_growth/api.h"
#include "svc_growth/sync.h"
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
    char report[PET_GROWTH_SYNC_MAX];
    int bytes=pet_growth_sync_format(device_id,boot_id,++sequence,epoch,&snapshot,report,sizeof(report));
    if(bytes>0&&pet_net_publish_topic(PET_NET_TOPIC_GROWTH_STATUS,report,(size_t)bytes,false)==ESP_OK)last_report=epoch;
}
#else
void app_growth_sync_init(const char *device){(void)device;}
void app_growth_sync_poll(uint32_t epoch){(void)epoch;}
#endif
