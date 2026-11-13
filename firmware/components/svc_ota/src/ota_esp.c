#include "sdkconfig.h"
#if CONFIG_PET_OTA_ENABLED
#include "svc_ota/api.h"
#include "svc_ota/transfer.h"
#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "pet/storage.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "psa/crypto.h"
#include "nvs.h"
#if CONFIG_PET_AI_ESP_SR_ENABLED
#include "svc_ai_agent/sr.h"
#endif
extern const uint8_t public_key_start[] asm("_binary_ota_public_key_start");
extern const uint8_t public_key_end[] asm("_binary_ota_public_key_end");
static QueueHandle_t jobs;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static pet_ota_snapshot_t view;
static _Atomic uint32_t sequence;
static bool pending_verify,health_finished,model_checked,model_valid;
static uint64_t health_started,healthy_since;
static uint64_t now_ms(void){return (uint64_t)esp_timer_get_time()/1000;}
static void publish(pet_ota_state_t state,esp_err_t error,uint32_t written)
{portENTER_CRITICAL(&lock);view=(pet_ota_snapshot_t){state,error,written,sequence};portEXIT_CRITICAL(&lock);}
static bool verify(void *ctx,const uint8_t *data,size_t bytes,const uint8_t *signature,size_t count)
{
    (void)ctx;psa_key_attributes_t attributes=PSA_KEY_ATTRIBUTES_INIT;mbedtls_svc_key_id_t key=0;
    psa_set_key_type(&attributes,PSA_KEY_TYPE_RSA_PUBLIC_KEY);psa_set_key_bits(&attributes,3072);
    psa_set_key_usage_flags(&attributes,PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attributes,PSA_ALG_RSA_PSS(PSA_ALG_SHA_256));
    bool ok=psa_crypto_init()==PSA_SUCCESS&&psa_import_key(&attributes,public_key_start,
        (size_t)(public_key_end-public_key_start),&key)==PSA_SUCCESS;
    if(ok)ok=psa_verify_message(key,PSA_ALG_RSA_PSS(PSA_ALG_SHA_256),data,bytes,signature,count)==PSA_SUCCESS;
    if(key)psa_destroy_key(key);
    psa_reset_key_attributes(&attributes);return ok;
}
static bool within(esp_http_client_handle_t http,uint64_t deadline)
{
    uint64_t now=now_ms();if(now>=deadline)return false;
    uint64_t remaining=deadline-now;
    return esp_http_client_set_timeout_ms(http,(int)(remaining<2000?remaining:2000))==ESP_OK;
}
static esp_http_client_handle_t open_url(const char *url,uint64_t deadline,int64_t *length)
{
    const esp_http_client_config_t config={.url=url,.timeout_ms=2000,.disable_auto_redirect=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.buffer_size=1024};
    esp_http_client_handle_t http=esp_http_client_init(&config);if(!http)return NULL;
    if(!within(http,deadline)||esp_http_client_open(http,0)!=ESP_OK||!within(http,deadline))goto failed;
    *length=esp_http_client_fetch_headers(http);
    if(*length<0||esp_http_client_get_status_code(http)!=200)goto failed;
    return http;
failed:
    esp_http_client_close(http);esp_http_client_cleanup(http);return NULL;
}
static void close_http(esp_http_client_handle_t http)
{if(http){esp_http_client_close(http);esp_http_client_cleanup(http);}}
static bool hash_partition(const esp_partition_t *part,size_t bytes,const char *expected,bool check_marker,uint8_t *block)
{
    uint8_t digest[32];size_t digest_bytes=0,carry=0;
    psa_hash_operation_t operation=PSA_HASH_OPERATION_INIT;
    if(psa_crypto_init()!=PSA_SUCCESS||psa_hash_setup(&operation,PSA_ALG_SHA_256)!=PSA_SUCCESS)return false;
    bool found=!check_marker,ok=false;size_t marker_len=strlen(pet_ota_identity_marker());
    for(size_t at=0;at<bytes;) {
        size_t count=bytes-at<4096?bytes-at:4096;
        if(esp_partition_read(part,at,block+carry,count)!=ESP_OK||psa_hash_update(&operation,block+carry,count)!=PSA_SUCCESS)goto done;
        if(!found)for(size_t i=0;i+marker_len<=carry+count;++i)
            if(!memcmp(block+i,pet_ota_identity_marker(),marker_len)){found=true;break;}
        size_t tail=marker_len-1;if(tail>carry+count)tail=carry+count;
        memmove(block,block+carry+count-tail,tail);carry=tail;at+=count;
        vTaskDelay(1);
    }
    if(psa_hash_finish(&operation,digest,sizeof(digest),&digest_bytes)!=PSA_SUCCESS||digest_bytes!=32||!found)goto done;
    char actual[65];for(unsigned i=0;i<32;++i)snprintf(actual+2*i,3,"%02x",digest[i]);
    ok=!strcmp(actual,expected);
done:
    psa_hash_abort(&operation);return ok;
}
static esp_err_t reserve_sequence(uint32_t next)
{
    uint8_t bytes[4],check[4];size_t count=0;for(unsigned i=0;i<4;++i)bytes[i]=(uint8_t)(next>>(8*i));
    (void)pet_storage_save("pet_ota","sequence",bytes,4);
    if(pet_storage_load("pet_ota","sequence",check,4,&count)!=ESP_OK||count!=4||memcmp(bytes,check,4))return ESP_FAIL;
    sequence=next;return ESP_OK;
}
typedef struct {
    const char *url;const esp_partition_t *partition;uint64_t deadline;
    esp_http_client_handle_t http;esp_ota_handle_t handle;uint8_t *buffer;
} download_t;
static uint64_t transfer_now(void *ctx){(void)ctx;return now_ms();}
static bool transfer_begin(void *ctx,const pet_ota_manifest_t *m)
{
    download_t *d=ctx;int64_t length=0;d->http=open_url(d->url,d->deadline,&length);
    return d->http&&length==m->image_size&&esp_ota_begin(d->partition,m->image_size,&d->handle)==ESP_OK;
}
static bool transfer_read(void *ctx,uint8_t *out,size_t cap,size_t *bytes)
{
    download_t *d=ctx;if(!within(d->http,d->deadline))return false;
    int got=esp_http_client_read(d->http,(char *)out,(int)cap);
    if(got<0)return false;
    *bytes=(size_t)got;return true;
}
static bool transfer_write(void *ctx,const uint8_t *data,size_t count)
{download_t *d=ctx;return esp_ota_write(d->handle,data,count)==ESP_OK;}
static bool transfer_finish(void *ctx)
{
    download_t *d=ctx;if(!esp_http_client_is_complete_data_received(d->http))return false;
    esp_err_t result=esp_ota_end(d->handle);d->handle=0;return result==ESP_OK;
}
static bool transfer_verify(void *ctx,const pet_ota_manifest_t *m)
{
    download_t *d=ctx;esp_app_desc_t description;
    return hash_partition(d->partition,m->image_size,m->image_sha256,true,d->buffer)&&
        esp_ota_get_partition_description(d->partition,&description)==ESP_OK&&
        !strncmp(description.project_name,pet_ota_identity()->project,sizeof(description.project_name))&&
        !strncmp(description.version,m->version,sizeof(description.version));
}
static bool transfer_reserve(void *ctx,uint32_t next){(void)ctx;return reserve_sequence(next)==ESP_OK;}
static bool transfer_commit(void *ctx){return esp_ota_set_boot_partition(((download_t *)ctx)->partition)==ESP_OK;}
static void transfer_cleanup(void *ctx)
{download_t *d=ctx;if(d->handle)esp_ota_abort(d->handle);close_http(d->http);d->handle=0;d->http=NULL;}
static void transfer_progress(void *ctx,uint32_t written)
{(void)ctx;publish(PET_OTA_DOWNLOADING,ESP_OK,written);vTaskDelay(1);}
static esp_err_t update(const pet_ota_target_t *target)
{
    typedef struct {char url[512],envelope[PET_OTA_ENVELOPE_MAX+1];uint8_t block[4096+256];} buffers_t;
    buffers_t *buffers=heap_caps_malloc(sizeof(*buffers),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffers)return ESP_ERR_NO_MEM;
    esp_err_t result=ESP_FAIL;esp_http_client_handle_t http=NULL;
    char *url=buffers->url,*envelope=buffers->envelope;int64_t length=0;
    uint64_t deadline=now_ms()+120000;size_t used=0;
    int n=snprintf(url,sizeof(buffers->url),"%s/%s/manifest.json",CONFIG_PET_OTA_BASE_URL,target->release_id);
    if(n<0||(size_t)n>=sizeof(buffers->url)){result=ESP_ERR_INVALID_ARG;goto done;}
    http=open_url(url,deadline,&length);if(!http||length<1||length>PET_OTA_ENVELOPE_MAX)goto done;
    while(used<(size_t)length) {
        if(!within(http,deadline))goto done;
        int got=esp_http_client_read(http,envelope+used,(int)((size_t)length-used));
        if(got<=0)goto done;
        used+=(size_t)got;
    }
    if(!esp_http_client_is_complete_data_received(http))goto done;
    close_http(http);http=NULL;
    pet_ota_manifest_t manifest;
    if(pet_ota_manifest_parse(envelope,used,pet_ota_identity(),sequence,verify,NULL,&manifest)!=PET_OTA_VALID)goto done;
    if(target->sequence&&(manifest.release_sequence!=target->sequence||manifest.image_size!=target->image_size||
        strcmp(manifest.image_sha256,target->image_sha256)||strcmp(manifest.version,target->version)))goto done;
    const esp_partition_t *running=esp_ota_get_running_partition(),*next=esp_ota_get_next_update_partition(NULL);
    esp_ota_img_states_t state;
    if(!running||!next||running->address==next->address||next->size<manifest.image_size||
        (esp_ota_get_state_partition(running,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY))goto done;
    n=snprintf(url,sizeof(buffers->url),"%s/%s/%s",CONFIG_PET_OTA_BASE_URL,target->release_id,manifest.image);
    if(n<0||(size_t)n>=sizeof(buffers->url))goto done;
    download_t download={.url=url,.partition=next,.deadline=deadline,.buffer=buffers->block};
    const pet_ota_transfer_port_t port={&download,transfer_now,transfer_begin,transfer_read,transfer_write,
        transfer_finish,transfer_verify,transfer_reserve,transfer_commit,transfer_cleanup,transfer_progress};
    result=pet_ota_transfer(&manifest,&port,deadline,buffers->block,4096)?ESP_OK:ESP_FAIL;
done:
    close_http(http);heap_caps_free(buffers);return result;
}
static void worker(void *arg)
{
    (void)arg;pet_ota_target_t target;
    for(;;)if(xQueueReceive(jobs,&target,portMAX_DELAY)==pdTRUE) {
        if(!target.release_id[0]){vTaskDelay(pdMS_TO_TICKS(500));esp_restart();}
        esp_err_t result=update(&target);pet_ota_snapshot_t current;pet_ota_snapshot(&current);
        publish(result==ESP_OK?PET_OTA_PENDING_REBOOT:PET_OTA_FAILED,result,current.written_bytes);
    }
}
esp_err_t pet_ota_start(void)
{
    if(jobs)return ESP_ERR_INVALID_STATE;
    if(strncmp(CONFIG_PET_OTA_BASE_URL,"https://",8)||!CONFIG_PET_OTA_BASE_URL[8]||strpbrk(CONFIG_PET_OTA_BASE_URL+8,"@?# \r\n"))return ESP_ERR_INVALID_ARG;
    uint8_t bytes[4];size_t count=0;esp_err_t error=pet_storage_init();if(error!=ESP_OK)return error;
    error=pet_storage_load("pet_ota","sequence",bytes,sizeof(bytes),&count);
    if(error==ESP_OK){if(count!=4)return ESP_ERR_INVALID_STATE;for(unsigned i=0;i<4;++i)sequence|=(uint32_t)bytes[i]<<(8*i);}
    else if(error!=ESP_ERR_NVS_NOT_FOUND)return error;
    jobs=xQueueCreate(1,sizeof(pet_ota_target_t));if(!jobs)return ESP_ERR_NO_MEM;
    publish(PET_OTA_IDLE,ESP_OK,0);
    if(xTaskCreate(worker,"pet_ota",16384,NULL,2,NULL)!=pdPASS){vQueueDelete(jobs);jobs=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
static esp_err_t enqueue(const pet_ota_target_t *target)
{
    if(!jobs)return ESP_ERR_INVALID_STATE;
    const char *release=target->release_id;
    if(!release||!release[0]||strlen(release)>64)return ESP_ERR_INVALID_ARG;
    for(size_t i=0;release[i];++i) {
        char c=release[i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='-'||c=='_'))return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&lock);
    bool blocked=view.state==PET_OTA_DOWNLOADING||view.state==PET_OTA_PENDING_REBOOT;
    if(!blocked)view=(pet_ota_snapshot_t){PET_OTA_DOWNLOADING,ESP_OK,0,sequence};
    portEXIT_CRITICAL(&lock);if(blocked)return ESP_ERR_INVALID_STATE;
    if(xQueueSend(jobs,target,0)!=pdTRUE){publish(PET_OTA_FAILED,ESP_ERR_TIMEOUT,0);return ESP_ERR_TIMEOUT;}return ESP_OK;
}
esp_err_t pet_ota_apply(const char *release)
{
    if(!release||!release[0]||strlen(release)>64)return ESP_ERR_INVALID_ARG;
    pet_ota_target_t target={0};strcpy(target.release_id,release);return enqueue(&target);
}
esp_err_t pet_ota_apply_target(const pet_ota_target_t *target)
{
    if(!target||!memchr(target->release_id,0,sizeof(target->release_id))||
        !memchr(target->version,0,sizeof(target->version))||!target->version[0]||
        target->image_sha256[64]||strlen(target->image_sha256)!=64||!target->sequence||
        !target->image_size||target->image_size>4*1024*1024)return ESP_ERR_INVALID_ARG;
    for(unsigned i=0;i<64;++i)if(!((target->image_sha256[i]>='0'&&target->image_sha256[i]<='9')||
        (target->image_sha256[i]>='a'&&target->image_sha256[i]<='f')))return ESP_ERR_INVALID_ARG;
    if(target->sequence<=sequence)return ESP_ERR_INVALID_STATE;
    return enqueue(target);
}
pet_ota_image_state_t pet_ota_running_target(const pet_ota_target_t *target)
{
    if(!target)return PET_OTA_IMAGE_UNKNOWN;
    // Active image verification is expensive; cache the immutable bytes per boot and target.
    static pet_ota_target_t checked;static bool known,matched;
    const esp_partition_t *part=esp_ota_get_running_partition();
    if(!part)return PET_OTA_IMAGE_UNKNOWN;
    if(!known||memcmp(&checked,target,sizeof(checked))) {
        uint8_t *block=heap_caps_malloc(4096+256,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!block)return PET_OTA_IMAGE_UNKNOWN;
        esp_app_desc_t desc;
        matched=target->image_size<=part->size&&esp_ota_get_partition_description(part,&desc)==ESP_OK&&
            !strcmp(desc.version,target->version)&&hash_partition(part,target->image_size,target->image_sha256,true,block);
        heap_caps_free(block);checked=*target;known=true;
    }
    if(!matched)return PET_OTA_IMAGE_OTHER;
    esp_ota_img_states_t state;
    if(esp_ota_get_state_partition(part,&state)!=ESP_OK)return PET_OTA_IMAGE_UNKNOWN;
    if(state==ESP_OTA_IMG_PENDING_VERIFY)return PET_OTA_IMAGE_PENDING;
    return state==ESP_OTA_IMG_VALID?PET_OTA_IMAGE_HEALTHY:PET_OTA_IMAGE_UNKNOWN;
}
esp_err_t pet_ota_snapshot(pet_ota_snapshot_t *out)
{if(!out)return ESP_ERR_INVALID_ARG;portENTER_CRITICAL(&lock);*out=view;portEXIT_CRITICAL(&lock);return jobs?ESP_OK:ESP_ERR_INVALID_STATE;}
esp_err_t pet_ota_reboot(void)
{
    pet_ota_snapshot_t current;if(pet_ota_snapshot(&current)!=ESP_OK||current.state!=PET_OTA_PENDING_REBOOT)return ESP_ERR_INVALID_STATE;
    pet_ota_target_t reboot={0};return xQueueSend(jobs,&reboot,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
void pet_ota_health_tick(bool healthy)
{
    if(health_finished)return;
    uint64_t now=now_ms();
    if(!health_started) {
        health_started=now;esp_ota_img_states_t state;
        const esp_partition_t *part=esp_ota_get_running_partition();
        pending_verify=part&&esp_ota_get_state_partition(part,&state)==ESP_OK&&state==ESP_OTA_IMG_PENDING_VERIFY;
        if(!pending_verify){health_finished=true;return;}
    }
#if CONFIG_PET_AI_ESP_SR_ENABLED
    if(!model_checked){model_valid=pet_ai_sr_verify_models();model_checked=true;}
    healthy=healthy&&model_valid;
#else
    (void)model_checked;(void)model_valid;
#endif
    if(!healthy)healthy_since=0;else if(!healthy_since)healthy_since=now;
    if(healthy_since&&now-healthy_since>=10000) {
        health_finished=esp_ota_mark_app_valid_cancel_rollback()==ESP_OK;
    }
    if(!health_finished&&now-health_started>=30000) {
        (void)esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
#endif
