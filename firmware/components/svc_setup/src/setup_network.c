#include "setup_internal.h"
#include "setup_policy.h"
#include "svc_net/netif_ip.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static void network_event(void *context,esp_event_base_t base,int32_t id,void *data)
{
    (void)data;
    setup_runtime_t *s=context;
    if(base==IP_EVENT&&id==IP_EVENT_STA_GOT_IP) xEventGroupSetBits(s->network_events,SETUP_CONNECTED);
    else if(base==WIFI_EVENT&&id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s->network_events,SETUP_CONNECTED);
        if(atomic_load(&s->trying)) xEventGroupSetBits(s->network_events,SETUP_DISCONNECTED);
    }
}

esp_err_t setup_network_start(setup_runtime_t *s)
{
    esp_err_t err=esp_netif_init();
    if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE) return err;
    err=esp_event_loop_create_default();
    if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE) return err;
    s->sta=esp_netif_create_default_wifi_sta();s->ap=esp_netif_create_default_wifi_ap();
    if(!s->sta||!s->ap) return ESP_ERR_NO_MEM;
    /* 家庭网络地址在首次连接前生效；坏值由校验拒绝，这里只记录并不阻断热点。 */
    esp_err_t address=pet_netif_apply_ip(s->sta,s->netcfg.ip_mode,s->netcfg.ip,s->netcfg.netmask,
        s->netcfg.gateway,s->netcfg.dns,s->netcfg.hostname);
    if(address!=ESP_OK) ESP_LOGW("pet_setup","home network address ignored: %s",esp_err_to_name(address));
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if((err=esp_wifi_init(&init))!=ESP_OK) return err;
    if((err=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK) return err;
    if((err=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK) return err;
    wifi_config_t empty={0};
    if((err=esp_wifi_set_config(WIFI_IF_STA,&empty))!=ESP_OK) return err;
    if((err=esp_wifi_start())!=ESP_OK) return err;
    /* RF 已运行，硬件 RNG 有连续熵源；默认策略每次进入重新生成名称、口令和会话 nonce。 */
    unsigned char random[32];esp_fill_random(random,sizeof(random));
    char ssid[PET_SETUP_SSID_MAX+1]={0},password[PET_SETUP_PASSWORD_MAX+1]={0};
    pet_setup_portal_effective(&s->portal,random,ssid,password);
    for(unsigned i=0;i<16;i++) snprintf(s->session+2*i,3,"%02x",random[i+14]);
    setup_wipe(random,sizeof(random));
    xSemaphoreTake(s->lock,portMAX_DELAY);
    memcpy(s->snapshot.ap_ssid,ssid,sizeof(ssid));
    memcpy(s->snapshot.ap_password,password,sizeof(password));
    s->snapshot.ap_hidden=s->portal.hidden;
    xSemaphoreGive(s->lock);
    wifi_config_t ap={0};
    memcpy(ap.ap.ssid,ssid,strlen(ssid));
    ap.ap.ssid_len=strlen(ssid);
    memcpy(ap.ap.password,password,strlen(password));
    ap.ap.authmode=password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    ap.ap.ssid_hidden=s->portal.hidden?1:0;
    ap.ap.max_connection=s->portal.max_clients;
    ap.ap.channel=s->portal.channel;
    setup_wipe(ssid,sizeof(ssid));setup_wipe(password,sizeof(password));
    if((err=esp_wifi_set_mode(WIFI_MODE_APSTA))!=ESP_OK) return err;
    if((err=esp_wifi_set_config(WIFI_IF_AP,&ap))!=ESP_OK) return err;
    if((err=esp_event_handler_register(WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,network_event,s))!=ESP_OK) return err;
    return esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,network_event,s);
}

esp_err_t setup_network_connect(setup_runtime_t *s,const char *ssid,const char *password)
{
    atomic_store(&s->trying,false);
    (void)esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));
    xEventGroupClearBits(s->network_events,SETUP_CONNECTED|SETUP_DISCONNECTED);
    wifi_config_t station={0};
    memcpy(station.sta.ssid,ssid,strlen(ssid));memcpy(station.sta.password,password,strlen(password));
    station.sta.threshold.authmode=password[0]?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&station);
    if(err!=ESP_OK) return err;
    atomic_store(&s->trying,true);
    err=esp_wifi_connect();
    if(err!=ESP_OK) {atomic_store(&s->trying,false);return err;}
    int64_t deadline=esp_timer_get_time()+30000000;
    while(!setup_cancelled(s)&&esp_timer_get_time()<deadline) {
        EventBits_t bits=xEventGroupWaitBits(s->network_events,SETUP_CONNECTED|SETUP_DISCONNECTED,pdFALSE,pdFALSE,pdMS_TO_TICKS(100));
        if(bits&SETUP_CONNECTED) {atomic_store(&s->trying,false);return ESP_OK;}
        if(bits&SETUP_DISCONNECTED) {atomic_store(&s->trying,false);return ESP_FAIL;}
    }
    atomic_store(&s->trying,false);(void)esp_wifi_disconnect();return ESP_ERR_TIMEOUT;
}
