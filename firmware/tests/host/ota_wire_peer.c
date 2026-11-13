/* Hosted OS/Flash/SDK boundary; commands run through the real remote OTA engine. */
#include "svc_ota/remote.h"
#include "cJSON.h"
#include "nvs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned char journal[2048];static size_t journal_bytes;
static unsigned installs,reboots;static bool restart;
static pet_ota_snapshot_t progress;
static pet_ota_image_state_t image=PET_OTA_IMAGE_OTHER;
static esp_err_t load(void *ctx,void *out,size_t capacity,size_t *bytes)
{(void)ctx;if(!journal_bytes)return ESP_ERR_NVS_NOT_FOUND;if(capacity<journal_bytes)return ESP_ERR_INVALID_SIZE;memcpy(out,journal,journal_bytes);*bytes=journal_bytes;return ESP_OK;}
static esp_err_t save(void *ctx,const void *data,size_t bytes)
{(void)ctx;assert(bytes<=sizeof(journal));memcpy(journal,data,bytes);journal_bytes=bytes;return ESP_OK;}
static esp_err_t apply(void *ctx,const pet_ota_target_t *target)
{(void)ctx;assert(journal_bytes);++installs;progress.state=PET_OTA_PENDING_REBOOT;progress.release_sequence=target->sequence;progress.written_bytes=target->image_size;return ESP_OK;}
static esp_err_t reboot(void *ctx){(void)ctx;assert(journal_bytes);++reboots;restart=true;return ESP_OK;}
static esp_err_t snapshot(void *ctx,pet_ota_snapshot_t *out){(void)ctx;*out=progress;return ESP_OK;}
static pet_ota_image_state_t running(void *ctx,const pet_ota_target_t *target){(void)ctx;(void)target;return image;}
static bool allowed(void *ctx){(void)ctx;return true;}
static void report(pet_ota_remote_t *service,const char *ack)
{
    char status[1536];assert(pet_ota_remote_status(service,(uint32_t)time(NULL),status,sizeof(status))==ESP_OK);
    cJSON *out=cJSON_CreateObject();assert(out);
    if(ack)cJSON_AddItemToObject(out,"ack",cJSON_Parse(ack));cJSON_AddItemToObject(out,"status",cJSON_Parse(status));
    cJSON_AddNumberToObject(out,"installs",installs);cJSON_AddNumberToObject(out,"reboots",reboots);
    char *wire=cJSON_PrintUnformatted(out);assert(wire);puts(wire);cJSON_free(wire);cJSON_Delete(out);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;setvbuf(stdout,NULL,_IONBF,0);
    const pet_ota_identity_t identity={"nexus_pet","dodo-s3-lcd","none","aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1,4};
    const pet_ota_remote_port_t port={NULL,load,save,apply,reboot,snapshot,running,allowed};
    pet_ota_remote_t *service=NULL;
    assert(pet_ota_remote_init(&service,argv[1],"11111111111111111111111111111111",&identity,&port)==ESP_OK);report(service,NULL);
    char line[1100];while(fgets(line,sizeof(line),stdin)) {
        size_t bytes=strlen(line);if(bytes&&line[bytes-1]=='\n')line[--bytes]=0;
        char ack[512];(void)pet_ota_remote_command(service,line,bytes,false,(uint32_t)time(NULL),ack,sizeof(ack));
        if(restart) {
            pet_ota_remote_close(service);service=NULL;image=PET_OTA_IMAGE_HEALTHY;restart=false;
            assert(pet_ota_remote_init(&service,argv[1],"22222222222222222222222222222222",&identity,&port)==ESP_OK);
        }
        assert(pet_ota_remote_tick(service)==ESP_OK);report(service,ack);
    }
    pet_ota_remote_close(service);return 0;
}
