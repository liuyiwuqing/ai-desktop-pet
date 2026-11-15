#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "svc_setup/api.h"
#include "svc_config/provision.h"
#include "pet/storage.h"
#include "freertos/task.h"

static esp_err_t load_result=ESP_ERR_NVS_NOT_FOUND,save_result=ESP_OK,ssid_result=ESP_ERR_NVS_NOT_FOUND;
static uint32_t stored;
static size_t stored_bytes=sizeof(stored);
static const char *ssid="home";
static bool create_ok=true,write_visible=true;
static TaskFunction_t pending;
static unsigned restart_count,delay_ms;
static jmp_buf restarted;

esp_err_t pet_storage_save(const char *ns,const char *key,const void *data,size_t bytes)
{
    assert(!strcmp(ns,"pet_setup")&&!strcmp(key,"boot_v1")&&bytes==sizeof(stored));
    if(write_visible&&save_result==ESP_OK) {memcpy(&stored,data,bytes);stored_bytes=bytes;load_result=ESP_OK;}
    return save_result;
}
esp_err_t pet_storage_load(const char *ns,const char *key,void *data,size_t cap,size_t *bytes)
{
    assert(!strcmp(ns,"pet_setup")&&!strcmp(key,"boot_v1")&&cap>=sizeof(stored));
    if(load_result==ESP_OK) {memcpy(data,&stored,sizeof(stored));*bytes=stored_bytes;}
    return load_result;
}
esp_err_t pet_provision_load_str(const char *ns,const char *key,char *out,size_t cap)
{
    assert(!strcmp(ns,"net")&&!strcmp(key,"wifi_ssid"));
    if(ssid_result==ESP_OK) {assert(strlen(ssid)<cap);strcpy(out,ssid);}
    return ssid_result;
}
int xTaskCreate(TaskFunction_t fn,const char *name,uint32_t stack,void *context,unsigned priority,TaskHandle_t *task)
{
    (void)name;(void)stack;(void)context;(void)priority;(void)task;
    if(!create_ok)return 0;
    pending=fn;return pdPASS;
}
void vTaskDelay(TickType_t ticks) {delay_ms+=(unsigned)ticks;}
void vTaskDelete(TaskHandle_t task) {(void)task;}
void esp_restart(void) {restart_count++;longjmp(restarted,1);}

int main(void)
{
    bool start=false;
    assert(pet_setup_should_start(NULL)==ESP_ERR_INVALID_ARG);
    assert(pet_setup_should_start(&start)==ESP_OK&&start==(CONFIG_PET_NET_WIFI_SSID[0]==0));
    ssid_result=ESP_OK;
    assert(pet_setup_should_start(&start)==ESP_OK&&!start);
    ssid="";assert(pet_setup_should_start(&start)==ESP_OK&&start);
    ssid_result=ESP_ERR_INVALID_STATE;
    assert(pet_setup_should_start(&start)==ESP_ERR_INVALID_STATE&&!start);
    load_result=ESP_OK;stored=0x51544302; /* 明确离线，即使没有 Wi-Fi 也不反复弹配网。 */
    assert(pet_setup_should_start(&start)==ESP_OK&&!start);
    stored=0x51544301;
    assert(pet_setup_should_start(&start)==ESP_OK&&start);
    stored=0x51544303;
    assert(pet_setup_should_start(&start)==ESP_ERR_INVALID_STATE&&!start);
    stored=0xdeadbeef;
    assert(pet_setup_should_start(&start)==ESP_ERR_INVALID_STATE);
    stored=0x51544301;stored_bytes=3;
    assert(pet_setup_should_start(&start)==ESP_ERR_INVALID_STATE);
    stored_bytes=4;load_result=ESP_FAIL;
    assert(pet_setup_should_start(&start)==ESP_FAIL);

    create_ok=false;assert(pet_setup_request()==ESP_ERR_NO_MEM);
    create_ok=true;save_result=ESP_FAIL;
    assert(pet_setup_request()==ESP_OK&&pending);
    assert(pet_setup_request()==ESP_ERR_INVALID_STATE);
    pending(NULL);assert(!restart_count&&pet_setup_request_failed()&&!pet_setup_request_failed());
    save_result=ESP_OK;write_visible=false;load_result=ESP_OK;stored=0x51544302;
    assert(pet_setup_request()==ESP_OK);pending(NULL);
    assert(!restart_count&&pet_setup_request_failed()); /* 写返回成功、回读不一致也不重启。 */
    write_visible=true;
    assert(pet_setup_request()==ESP_OK);
    if(!setjmp(restarted)) pending(NULL);
    assert(restart_count==1&&delay_ms==800&&stored==0x51544301);
    assert(pet_setup_should_start(&start)==ESP_OK&&start);
    puts("Setup boot: first start, offline persistence, compiled fallback, corruption and queued/retried request passed");
    return 0;
}
