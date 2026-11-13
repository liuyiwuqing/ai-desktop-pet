/* Protocol peer for Java/MQTT interoperability: real config service, RAM flash boundary. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "svc_config/api.h"
#include "pet/storage.h"
#include "freertos/semphr.h"

static unsigned char flash[1024];
static size_t flash_bytes;
static unsigned writes;
esp_err_t pet_storage_init(void) { return ESP_OK; }
esp_err_t pet_storage_load(const char *ns,const char *key,void *out,size_t capacity,size_t *bytes)
{
    assert(!strcmp(ns,"pet_cfg")&&!strcmp(key,"settings_v1"));
    if(!flash_bytes)return ESP_ERR_NVS_NOT_FOUND;
    if(capacity<flash_bytes)return ESP_ERR_INVALID_SIZE;
    memcpy(out,flash,flash_bytes);*bytes=flash_bytes;return ESP_OK;
}
esp_err_t pet_storage_save(const char *ns,const char *key,const void *input,size_t bytes)
{
    assert(!strcmp(ns,"pet_cfg")&&!strcmp(key,"settings_v1")&&bytes<=sizeof(flash));
    memcpy(flash,input,bytes);flash_bytes=bytes;++writes;return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ pthread_mutex_t *p=malloc(sizeof(*p));assert(p&&!pthread_mutex_init(p,NULL));return p; }
int xSemaphoreTake(SemaphoreHandle_t p,uint32_t timeout)
{ return !(timeout?pthread_mutex_lock(p):pthread_mutex_trylock(p)); }
int xSemaphoreGive(SemaphoreHandle_t p) { return !pthread_mutex_unlock(p); }
void vSemaphoreDelete(SemaphoreHandle_t p) { pthread_mutex_destroy(p);free(p); }

int main(int argc,char **argv)
{
    if(argc!=2||pet_config_init()!=ESP_OK)return 2;
    char line[1202],reply[512],config[PET_CONFIG_JSON_MAX];
    while(fgets(line,sizeof(line),stdin)) {
        size_t bytes=strlen(line);
        if(bytes&&line[bytes-1]=='\n')line[--bytes]=0;
        else if(!feof(stdin))return 3;
        (void)pet_config_remote(argv[1],line,bytes,false,(uint32_t)time(NULL),reply,sizeof(reply));
        pet_config_snapshot_t state;size_t used;
        if(pet_config_snapshot(&state)!=ESP_OK||pet_config_format_json(&state.config,config,sizeof(config),&used)!=ESP_OK)return 4;
        printf("{\"ack\":%s,\"revision\":%lu,\"config\":%s,\"writes\":%u}\n",reply,(unsigned long)state.revision,config,writes);
        fflush(stdout);
    }
    pet_config_close();return 0;
}
