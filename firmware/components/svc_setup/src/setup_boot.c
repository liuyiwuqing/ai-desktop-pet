#include "svc_setup/api.h"
#include <stdatomic.h>
#include "sdkconfig.h"
#include "svc_config/provision.h"
#include "pet/storage.h"
#include "nvs.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_bool requested, request_failed;

esp_err_t setup_boot_save(uint32_t mode)
{
    const uint32_t value=0x51544300u|mode;
    esp_err_t err=pet_storage_save("pet_setup","boot_v1",&value,sizeof(value));
    uint32_t actual=0;size_t bytes=0;
    esp_err_t checked=pet_storage_load("pet_setup","boot_v1",&actual,sizeof(actual),&bytes);
    return checked==ESP_OK&&bytes==sizeof(actual)&&actual==value?ESP_OK:err==ESP_OK?ESP_FAIL:err;
}

esp_err_t pet_setup_should_start(bool *start)
{
    if(!start) return ESP_ERR_INVALID_ARG;
    *start=false;
    uint32_t mode=0;size_t bytes=0;
    esp_err_t err=pet_storage_load("pet_setup","boot_v1",&mode,sizeof(mode),&bytes);
    if(err==ESP_OK) {
        if(bytes!=sizeof(mode)||(mode&0xffffff00u)!=0x51544300u||(mode&255u)>2) return ESP_ERR_INVALID_STATE;
        mode&=255u;
    } else if(err!=ESP_ERR_NVS_NOT_FOUND) return err;
    if(mode==1) {*start=true;return ESP_OK;}
    if(mode==2) return ESP_OK; /* 用户明确选择离线使用，避免每次重启强迫配网。 */
    char ssid[33]={0};
    err=pet_provision_load_str("net","wifi_ssid",ssid,sizeof(ssid));
    if(err==ESP_ERR_NVS_NOT_FOUND) {
        *start=CONFIG_PET_NET_WIFI_SSID[0]==0;
        return ESP_OK;
    }
    if(err==ESP_OK) *start=ssid[0]==0;
    volatile unsigned char *secret=(volatile unsigned char *)ssid;
    for(size_t i=0;i<sizeof(ssid);i++) secret[i]=0;
    return err;
}

static void request_task(void *unused)
{
    (void)unused;
    if(setup_boot_save(1)==ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(800));
        esp_restart();
    }
    atomic_store(&request_failed,true);atomic_store(&requested,false);vTaskDelete(NULL);
}

esp_err_t pet_setup_request(void)
{
    if(atomic_exchange(&requested,true)) return ESP_ERR_INVALID_STATE;
    atomic_store(&request_failed,false);
    if(xTaskCreate(request_task,"pet_setup_open",4096,NULL,2,NULL)!=pdPASS) {
        atomic_store(&requested,false);return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool pet_setup_request_failed(void) {return atomic_exchange(&request_failed,false);}
