#include "setup_internal.h"
#include "setup_activation.h"
#include "svc_config/provision.h"
#include "pet/storage.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "esp_netif_sntp.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define XIAOZHI_REGISTRATION "https://api.tenclass.net/xiaozhi/ota/"
#define ACTIVATION_RESPONSE_MAX 16384u

typedef struct {char *data;size_t bytes;bool overflow;} response_t;
static esp_err_t response_event(esp_http_client_event_t *event)
{
    response_t *r=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_DATA&&event->data_len>0) {
        if((size_t)event->data_len>ACTIVATION_RESPONSE_MAX-r->bytes) {r->overflow=true;return ESP_ERR_INVALID_SIZE;}
        memcpy(r->data+r->bytes,event->data,event->data_len);r->bytes+=event->data_len;r->data[r->bytes]=0;
    }
    return ESP_OK;
}

static esp_err_t identity(char device[18],char client[37])
{
    unsigned char mac[6];esp_err_t err=esp_read_mac(mac,ESP_MAC_WIFI_STA);
    if(err!=ESP_OK) return err;
    snprintf(device,18,"%02x:%02x:%02x:%02x:%02x:%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    size_t bytes=0;
    err=pet_storage_load("pet_setup","client_v1",client,37,&bytes);
    if(err==ESP_OK) return bytes==37&&client[36]==0?ESP_OK:ESP_ERR_INVALID_STATE;
    if(err!=ESP_ERR_NVS_NOT_FOUND) return err;
    unsigned char uuid[16];esp_fill_random(uuid,sizeof(uuid));uuid[6]=(uuid[6]&15)|64;uuid[8]=(uuid[8]&63)|128;
    snprintf(client,37,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0],uuid[1],uuid[2],uuid[3],uuid[4],uuid[5],uuid[6],uuid[7],uuid[8],uuid[9],uuid[10],uuid[11],uuid[12],uuid[13],uuid[14],uuid[15]);
    return pet_storage_save("pet_setup","client_v1",client,37);
}

static esp_err_t request(const char *url,const char *device,const char *client,const char *body,response_t *response,int *status)
{
    response->bytes=0;response->overflow=false;response->data[0]=0;
    const esp_http_client_config_t config={.url=url,.crt_bundle_attach=esp_crt_bundle_attach,
        .timeout_ms=5000,.disable_auto_redirect=true,.event_handler=response_event,.user_data=response,
        .buffer_size=1024,.buffer_size_tx=1024,.method=HTTP_METHOD_POST};
    esp_http_client_handle_t http=esp_http_client_init(&config);
    if(!http) return ESP_ERR_NO_MEM;
    esp_err_t err=esp_http_client_set_header(http,"Content-Type","application/json");
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Activation-Version","1");
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Device-Id",device);
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Client-Id",client);
    if(err==ESP_OK) err=esp_http_client_set_header(http,"User-Agent","Qitomo/dodo-s3-lcd");
    if(err==ESP_OK) err=esp_http_client_set_header(http,"Accept-Language","zh-CN");
    if(err==ESP_OK) err=esp_http_client_set_post_field(http,body,strlen(body));
    if(err==ESP_OK) err=esp_http_client_perform(http);
    *status=esp_http_client_get_status_code(http);
    esp_http_client_cleanup(http);
    if(response->overflow) err=ESP_ERR_INVALID_SIZE;
    return err;
}

static esp_err_t save_voice(const pet_setup_activation_result_t *result,const char *device,const char *client)
{
    cJSON *root=cJSON_CreateObject(),*xz=root?cJSON_AddObjectToObject(root,"xiaozhi"):NULL;
    char *body=NULL;esp_err_t err=ESP_ERR_NO_MEM;uint32_t revision;
    if(xz&&cJSON_AddStringToObject(xz,"ws_uri",result->uri)&&cJSON_AddStringToObject(xz,"ws_token",result->token)&&
       cJSON_AddStringToObject(xz,"device_id",device)&&cJSON_AddStringToObject(xz,"client_id",client)) body=cJSON_PrintUnformatted(root);
    if(body) {
        err=pet_provision_revision(&revision);
        if(err==ESP_OK) err=pet_provision_patch(body,strlen(body),revision);
        setup_wipe(body,strlen(body));cJSON_free(body);
    }
    cJSON *token=cJSON_GetObjectItemCaseSensitive(xz,"ws_token");
    if(cJSON_IsString(token)) setup_wipe(token->valuestring,strlen(token->valuestring));
    cJSON_Delete(root);return err;
}

esp_err_t setup_activation_run(setup_runtime_t *s)
{
    if(!(xEventGroupGetBits(s->network_events)&SETUP_CONNECTED)) return ESP_ERR_INVALID_STATE;
    if(!s->sntp_started) {
        esp_sntp_config_t clock=ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        esp_err_t sync=esp_netif_sntp_init(&clock);
        if(sync!=ESP_OK) return sync;
        s->sntp_started=true;
    }
    int64_t clock_deadline=esp_timer_get_time()+20000000;
    while(time(NULL)<1577836800&&!setup_cancelled(s)&&esp_timer_get_time()<clock_deadline)
        vTaskDelay(pdMS_TO_TICKS(100));
    if(time(NULL)<1577836800||setup_cancelled(s)) return ESP_ERR_TIMEOUT;
    char device[18]={0},client[37]={0},payload[768];
    esp_err_t err=identity(device,client);
    if(err!=ESP_OK) return err;
    const esp_app_desc_t *app=esp_app_get_description();
    int size=snprintf(payload,sizeof(payload),
        "{\"version\":2,\"language\":\"zh-CN\",\"flash_size\":16777216,\"psram_size\":8388608,"
        "\"mac_address\":\"%s\",\"uuid\":\"%s\",\"chip_model_name\":\"esp32s3\","
        "\"application\":{\"name\":\"nexus_pet\",\"version\":\"%s\",\"idf_version\":\"%s\"},"
        "\"board\":{\"type\":\"dodo-s3-lcd\",\"name\":\"Qitomo\",\"mac\":\"%s\"}}",
        device,client,app->version,app->idf_ver,device);
    if(size<0||(size_t)size>=sizeof(payload)) return ESP_ERR_INVALID_SIZE;
    response_t response={.data=calloc(1,ACTIVATION_RESPONSE_MAX+1)};
    pet_setup_activation_result_t *result=calloc(1,sizeof(*result));
    if(!response.data||!result) {free(response.data);free(result);return ESP_ERR_NO_MEM;}
    int64_t deadline=esp_timer_get_time()+300000000;
    bool challenge=false;
    while(!setup_cancelled(s)&&esp_timer_get_time()<deadline) {
        int status=0;
        err=request(XIAOZHI_REGISTRATION,device,client,payload,&response,&status);
        if(err!=ESP_OK||status!=200) {if(err==ESP_OK)err=ESP_FAIL;break;}
        err=pet_setup_activation_parse(response.data,response.bytes,result);
        setup_wipe(response.data,ACTIVATION_RESPONSE_MAX+1);
        if(err!=ESP_OK) break;
        if(!result->waiting) {err=save_voice(result,device,client);break;}
        challenge=result->has_challenge;
        xSemaphoreTake(s->lock,portMAX_DELAY);
        strcpy(s->snapshot.activation_code,result->code);s->snapshot.state=PET_SETUP_WAITING_BIND;
        xSemaphoreGive(s->lock);
        for(unsigned i=0;i<30&&!setup_cancelled(s);i++) vTaskDelay(pdMS_TO_TICKS(100));
        if(setup_cancelled(s)) {err=ESP_ERR_TIMEOUT;break;}
        if(challenge) {
            err=request(XIAOZHI_REGISTRATION "activate",device,client,"{}",&response,&status);
            setup_wipe(response.data,ACTIVATION_RESPONSE_MAX+1);
            if(err!=ESP_OK||(status!=200&&status!=202)) {if(err==ESP_OK)err=ESP_FAIL;break;}
        }
        err=ESP_ERR_TIMEOUT;
    }
    xSemaphoreTake(s->lock,portMAX_DELAY);setup_wipe(s->snapshot.activation_code,sizeof(s->snapshot.activation_code));xSemaphoreGive(s->lock);
    setup_wipe(result,sizeof(*result));free(result);
    setup_wipe(response.data,ACTIVATION_RESPONSE_MAX+1);free(response.data);
    setup_wipe(payload,sizeof(payload));setup_wipe(device,sizeof(device));setup_wipe(client,sizeof(client));
    return err;
}
