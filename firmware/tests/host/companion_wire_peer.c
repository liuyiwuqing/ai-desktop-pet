/* Real growth/store/codec behind RAM flash, used by the HTTP/PostgreSQL/MQTT contract test. */
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "svc_growth/api.h"
#include "svc_growth/companion_sync.h"
#include "pet/storage.h"
#include "pet/event_bus.h"
#include "freertos/semphr.h"
static struct {uint8_t data[256];size_t size;} blobs[3];
static unsigned writes;
static unsigned key(const char *name) {return !strcmp(name,"snap0")?0:!strcmp(name,"snap1")?1:2;}
esp_err_t pet_storage_init(void){return ESP_OK;}
esp_err_t pet_storage_load(const char *ns,const char *name,void *out,size_t cap,size_t *bytes)
{
    assert(!strcmp(ns,"pet_growth"));unsigned i=key(name);
    if(!blobs[i].size)return ESP_ERR_NVS_NOT_FOUND;
    if(cap<blobs[i].size)return ESP_ERR_INVALID_SIZE;
    memcpy(out,blobs[i].data,blobs[i].size);*bytes=blobs[i].size;return ESP_OK;
}
esp_err_t pet_storage_save(const char *ns,const char *name,const void *data,size_t bytes)
{
    assert(!strcmp(ns,"pet_growth")&&bytes<=256);unsigned i=key(name);
    memcpy(blobs[i].data,data,bytes);blobs[i].size=bytes;++writes;return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void){pthread_mutex_t *p=malloc(sizeof(*p));assert(p&&!pthread_mutex_init(p,NULL));return p;}
int xSemaphoreTake(SemaphoreHandle_t p,uint32_t wait){return !(wait?pthread_mutex_lock(p):pthread_mutex_trylock(p));}
int xSemaphoreGive(SemaphoreHandle_t p){return !pthread_mutex_unlock(p);}
void vSemaphoreDelete(SemaphoreHandle_t p){pthread_mutex_destroy(p);free(p);}
esp_err_t pet_event_bus_publish(const pet_event_t *event){assert(pet_event_is_valid(event));return ESP_OK;}
static uint64_t monotonic(void *context){(void)context;struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static uint32_t epoch(void *context){(void)context;return (uint32_t)time(NULL);}
static void report(const char *device,unsigned sequence,char *out)
{
    pet_growth_view_t growth;pet_companion_view_t companion;
    assert(pet_growth_get_snapshot(&growth)==PET_GROWTH_OK && pet_companion_snapshot(&companion)==PET_GROWTH_OK);
    assert(pet_companion_sync_format(device,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",sequence,epoch(NULL),growth.revision,&companion,out,PET_COMPANION_SYNC_MAX)>0);
}
int main(int argc,char **argv)
{
    if(argc!=2)return 2;pet_growth_clock_t clock={monotonic,epoch,NULL,480};
    if(pet_growth_init(&clock)!=PET_GROWTH_OK)return 3;
    pet_companion_request_t game={.request_id=1,.issued_at_ms=monotonic(NULL),.operation=PET_COMPANION_GAME,.game=0,.score=120};
    assert(pet_companion_submit(&game).result==PET_GROWTH_OK);
    pet_companion_request_t collect={.request_id=2,.issued_at_ms=monotonic(NULL),.operation=PET_COMPANION_BUY,.item=3};
    assert(pet_companion_submit(&collect).result==PET_GROWTH_OK);
    char line[PET_COMPANION_COMMAND_MAX+2],state[PET_COMPANION_SYNC_MAX],reply[256];unsigned sequence=1;
    report(argv[1],sequence,state);puts(state);fflush(stdout);
    while(fgets(line,sizeof(line),stdin)) {
        size_t bytes=strlen(line);if(bytes&&line[bytes-1]=='\n')line[--bytes]=0;else if(!feof(stdin))return 4;
        (void)pet_companion_remote(argv[1],line,bytes,false,epoch(NULL),monotonic(NULL),reply,sizeof(reply));
        report(argv[1],++sequence,state);printf("{\"ack\":%s,\"snapshot\":%s,\"writes\":%u}\n",reply,state,writes);fflush(stdout);
    }
    pet_growth_close();return 0;
}
