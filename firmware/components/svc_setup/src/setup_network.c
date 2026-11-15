#include "setup_internal.h"
#include "esp_event.h"
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
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    if((err=esp_wifi_init(&init))!=ESP_OK) return err;
    if((err=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK) return err;
    if((err=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK) return err;
    wifi_config_t empty={0};
    if((err=esp_wifi_set_config(WIFI_IF_STA,&empty))!=ESP_OK) return err;
    if((err=esp_wifi_start())!=ESP_OK) return err;
    /* RF 已运行，硬件 RNG 有连续熵源；密码和会话 nonce 每次进入配网重新生成。 */
    unsigned char random[32];esp_fill_random(random,sizeof(random));
    snprintf(s->snapshot.ap_ssid,sizeof(s->snapshot.ap_ssid),"Qitomo-%02X%02X",random[0],random[1]);
    static const char alphabet[]="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
    for(unsigned i=0;i<12;i++) s->snapshot.ap_password[i]=alphabet[random[i+2]&31];
    for(unsigned i=0;i<16;i++) snprintf(s->session+2*i,3,"%02x",random[i+14]);
    setup_wipe(random,sizeof(random));
    wifi_config_t ap={0};
    memcpy(ap.ap.ssid,s->snapshot.ap_ssid,strlen(s->snapshot.ap_ssid));
    ap.ap.ssid_len=strlen(s->snapshot.ap_ssid);
    memcpy(ap.ap.password,s->snapshot.ap_password,12);
    ap.ap.authmode=WIFI_AUTH_WPA2_PSK;ap.ap.max_connection=2;ap.ap.channel=1;
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
