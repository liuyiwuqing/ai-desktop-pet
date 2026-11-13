#include "svc_ota/remote.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nvs.h"

static unsigned char stored[2048];
static size_t stored_bytes;
static int applies,reboots;
static bool fail_save,write_then_error,can_apply=true;
static esp_err_t reboot_error;
static pet_ota_snapshot_t progress;
static pet_ota_image_state_t image;
static const pet_ota_identity_t identity={"nexus_pet","dodo-s3-lcd","none",
    "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1,4};
static esp_err_t load(void *ctx,void *out,size_t cap,size_t *bytes)
{(void)ctx;if(!stored_bytes)return ESP_ERR_NVS_NOT_FOUND;assert(cap>=stored_bytes);memcpy(out,stored,stored_bytes);*bytes=stored_bytes;return ESP_OK;}
static esp_err_t save(void *ctx,const void *data,size_t bytes)
{(void)ctx;if(fail_save)return ESP_FAIL;assert(bytes<=sizeof(stored));memcpy(stored,data,bytes);stored_bytes=bytes;return write_then_error?ESP_FAIL:ESP_OK;}
static esp_err_t apply(void *ctx,const pet_ota_target_t *target)
{(void)ctx;assert(stored_bytes>0);assert(target->sequence==10);applies++;progress.state=PET_OTA_DOWNLOADING;return ESP_OK;}
static esp_err_t reboot(void *ctx)
{(void)ctx;reboots++;return reboot_error;}
static esp_err_t snapshot(void *ctx,pet_ota_snapshot_t *out)
{(void)ctx;*out=progress;return ESP_OK;}
static pet_ota_image_state_t running(void *ctx,const pet_ota_target_t *target)
{(void)ctx;(void)target;return image;}
static bool allowed(void *ctx){(void)ctx;return can_apply;}
static const pet_ota_remote_port_t port={NULL,load,save,apply,reboot,snapshot,running,allowed};
static const char *boot="11111111111111111111111111111111";
static const char *target="{\"releaseId\":\"v04\",\"version\":\"0.4.1\",\"imageSha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\",\"imageSize\":1024,\"releaseSequence\":10}";
static void command(char *out,size_t size,const char *id,const char *cid,const char *operation,const char *nonce,unsigned issued)
{
    int n=snprintf(out,size,"{\"schemaVersion\":1,\"deviceId\":\"dodo-ota\",\"requestId\":\"%s\",\"commandId\":\"%s\",\"operation\":\"%s\",\"bootId\":\"%s\",\"issuedAt\":%u,\"expiresAt\":%u,\"target\":%s}",id,cid,operation,nonce,issued,issued+100,target);
    assert(n>0&&(size_t)n<size);
}
static bool accepted(pet_ota_remote_t *service,const char *wire,bool retained,unsigned now)
{
    char out[512];(void)pet_ota_remote_command(service,wire,strlen(wire),retained,now,out,sizeof(out));
    cJSON *value=cJSON_Parse(out);assert(value);
    bool ok=cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(value,"accepted"));cJSON_Delete(value);return ok;
}
static void state(pet_ota_remote_t *service,const char *expected)
{
    char out[1536];assert(pet_ota_remote_status(service,200,out,sizeof(out))==ESP_OK);
    cJSON *value=cJSON_Parse(out);assert(value);
    const cJSON *actual=cJSON_GetObjectItemCaseSensitive(value,"state");
    assert(cJSON_IsString(actual));if(strcmp(actual->valuestring,expected))fprintf(stderr,"Expected %s, got %s\n",expected,actual->valuestring);
    assert(!strcmp(actual->valuestring,expected));cJSON_Delete(value);
}
int main(void)
{
    pet_ota_remote_t *service=NULL;char wire[1025];
    assert(pet_ota_remote_init(&service,"dodo-ota",boot,&identity,&port)==ESP_OK);
    state(service,"idle");
    command(wire,sizeof(wire),"future","future","install",boot,120);
    assert(!accepted(service,wire,false,100));assert(applies==0&&stored_bytes==0);
    command(wire,sizeof(wire),"job-1","job-1","install",boot,100);
    assert(!accepted(service,wire,true,100));assert(applies==0&&stored_bytes==0);
    can_apply=false;assert(!accepted(service,wire,false,100));assert(applies==0&&stored_bytes==0);can_apply=true;
    assert(accepted(service,wire,false,100));assert(applies==1);state(service,"installing");
    command(wire,sizeof(wire),"another","another","install",boot,101);
    assert(!accepted(service,wire,false,101));assert(applies==1);
    command(wire,sizeof(wire),"job-1","job-1","install",boot,100);
    assert(accepted(service,wire,false,101));assert(applies==1);
    assert(!accepted(service,wire,true,101));assert(applies==1);
    progress.state=PET_OTA_PENDING_REBOOT;progress.release_sequence=10;progress.written_bytes=1024;
    assert(pet_ota_remote_tick(service)==ESP_OK);state(service,"pending_reboot");
    command(wire,sizeof(wire),"job-1","reboot-1","reboot",boot,102);
    assert(accepted(service,wire,false,102));assert(reboots==1);state(service,"reboot_requested");
    assert(accepted(service,wire,false,103));assert(reboots==1);
    pet_ota_remote_close(service);service=NULL;
    image=PET_OTA_IMAGE_PENDING;
    assert(pet_ota_remote_init(&service,"dodo-ota","22222222222222222222222222222222",&identity,&port)==ESP_OK);
    state(service,"verifying");assert(applies==1&&reboots==1);
    image=PET_OTA_IMAGE_HEALTHY;assert(pet_ota_remote_tick(service)==ESP_OK);state(service,"healthy");
    assert(!accepted(service,wire,false,104));assert(reboots==1);
    pet_ota_remote_close(service);service=NULL;
    /* A later boot must recheck the active image even after a prior healthy report. */
    image=PET_OTA_IMAGE_OTHER;
    assert(pet_ota_remote_init(&service,"dodo-ota","33333333333333333333333333333333",&identity,&port)==ESP_OK);
    state(service,"rolled_back");assert(applies==1&&reboots==1);
    pet_ota_remote_close(service);service=NULL;
    stored_bytes=0;progress=(pet_ota_snapshot_t){0};applies=0;fail_save=true;
    assert(pet_ota_remote_init(&service,"dodo-ota",boot,&identity,&port)==ESP_OK);
    command(wire,sizeof(wire),"job-2","job-2","install",boot,110);
    assert(!accepted(service,wire,false,110));assert(applies==0);
    pet_ota_remote_close(service);service=NULL;
    fail_save=false;write_then_error=true;
    assert(pet_ota_remote_init(&service,"dodo-ota",boot,&identity,&port)==ESP_OK);
    assert(accepted(service,wire,false,110));assert(applies==1);
    progress.state=PET_OTA_PENDING_REBOOT;progress.release_sequence=10;
    assert(pet_ota_remote_tick(service)==ESP_OK);reboot_error=ESP_ERR_TIMEOUT;
    command(wire,sizeof(wire),"job-2","reboot-2","reboot",boot,111);
    assert(!accepted(service,wire,false,111));state(service,"unconfirmed");
    int attempted=reboots;assert(accepted(service,wire,false,112));assert(reboots==attempted);
    pet_ota_remote_close(service);service=NULL;stored[14]^=1;
    assert(pet_ota_remote_init(&service,"dodo-ota",boot,&identity,&port)==ESP_ERR_INVALID_STATE);
    state(service,"unavailable");assert(!accepted(service,wire,false,112));assert(reboots==attempted);
    pet_ota_remote_close(service);
    puts("Remote OTA: durable actions, duplicate/boot/time isolation, health, corruption and storage/reboot uncertainty passed");
    return 0;
}
