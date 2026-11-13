#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "svc_config/api.h"
#include "svc_config/provision.h"
#include "pet/storage.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static unsigned char saved[1024];
static size_t saved_size;
static bool fail_write, fail_read;
static unsigned writes;
static unsigned char credentials[11000];
static size_t credentials_size;
esp_err_t pet_storage_init(void) { return ESP_OK; }
esp_err_t pet_storage_load(const char *ns, const char *key, void *out, size_t cap, size_t *size)
{
    if (!strcmp(ns,"pet_provision")) {
        assert(!strcmp(key,"bundle_v1"));
        if (!credentials_size) return ESP_ERR_NVS_NOT_FOUND;
        if (cap<credentials_size) return ESP_ERR_INVALID_SIZE;
        *size=credentials_size; memcpy(out,credentials,*size); return ESP_OK;
    }
    assert(!strcmp(ns, "pet_cfg") && !strcmp(key, "settings_v1"));
    if (fail_read) return ESP_FAIL;
    if (!saved_size) return ESP_ERR_NVS_NOT_FOUND;
    if (cap < saved_size) return ESP_ERR_INVALID_SIZE;
    memcpy(out, saved, saved_size); *size = saved_size; return ESP_OK;
}
esp_err_t pet_storage_save(const char *ns, const char *key, const void *data, size_t size)
{
    if (!strcmp(ns,"pet_provision")) {
        assert(!strcmp(key,"bundle_v1")&&size<=sizeof(credentials));
        if (fail_write) return ESP_FAIL;
        memcpy(credentials,data,size); credentials_size=size; return ESP_OK;
    }
    assert(!strcmp(ns, "pet_cfg") && !strcmp(key, "settings_v1"));
    ++writes;
    if (fail_write) return ESP_FAIL;
    assert(size <= sizeof(saved)); memcpy(saved, data, size); saved_size = size; return ESP_OK;
}
esp_err_t pet_storage_load_str(const char *ns, const char *key, char *out, size_t cap)
{ (void)ns; (void)key; (void)out; (void)cap; return ESP_ERR_NVS_NOT_FOUND; }
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *m = malloc(sizeof(*m)); assert(m && !pthread_mutex_init(m, NULL)); return m;
}
int xSemaphoreTake(SemaphoreHandle_t m, uint32_t timeout)
{ return !(timeout ? pthread_mutex_lock(m) : pthread_mutex_trylock(m)); }
int xSemaphoreGive(SemaphoreHandle_t m) { return !pthread_mutex_unlock(m); }
void vSemaphoreDelete(SemaphoreHandle_t m) { pthread_mutex_destroy(m); free(m); }

/* Deliberately pause the RTOS task after enqueue, before its first Flash write. */
typedef struct { pthread_mutex_t lock;pthread_cond_t ready;size_t width;void *item;bool pending,stop; } fake_queue_t;
static fake_queue_t *settings_queue;
static TaskFunction_t settings_task;
static void *settings_context;
static bool queue_send_failed;
QueueHandle_t xQueueCreate(unsigned length,unsigned width)
{
    assert(length==1);settings_queue=calloc(1,sizeof(*settings_queue));assert(settings_queue);
    assert(!pthread_mutex_init(&settings_queue->lock,NULL)&&!pthread_cond_init(&settings_queue->ready,NULL));
    settings_queue->width=width;settings_queue->item=malloc(width);assert(settings_queue->item);return settings_queue;
}
void vQueueDelete(QueueHandle_t handle)
{
    fake_queue_t *q=handle;pthread_mutex_destroy(&q->lock);pthread_cond_destroy(&q->ready);free(q->item);free(q);
}
int xQueueSend(QueueHandle_t handle,const void *value,TickType_t timeout)
{
    (void)timeout;fake_queue_t *q=handle;pthread_mutex_lock(&q->lock);
    bool accepted=!q->pending&&!queue_send_failed;
    if(accepted) {memcpy(q->item,value,q->width);q->pending=true;pthread_cond_signal(&q->ready);}
    pthread_mutex_unlock(&q->lock);return accepted;
}
int xQueueReceive(QueueHandle_t handle,void *value,TickType_t timeout)
{
    (void)timeout;fake_queue_t *q=handle;pthread_mutex_lock(&q->lock);
    while(!q->pending&&!q->stop)pthread_cond_wait(&q->ready,&q->lock);
    if(q->stop) {pthread_mutex_unlock(&q->lock);pthread_exit(NULL);}
    memcpy(value,q->item,q->width);q->pending=false;pthread_mutex_unlock(&q->lock);return pdTRUE;
}
int xTaskCreate(TaskFunction_t fn,const char *name,uint32_t stack,void *context,unsigned priority,TaskHandle_t *task)
{(void)name;(void)stack;(void)priority;(void)task;settings_task=fn;settings_context=context;return pdPASS;}
static void *run_settings(void *unused) {(void)unused;settings_task(settings_context);return NULL;}

static const char json[] = "{\"schemaVersion\":1,\"timezone\":\"Asia/Shanghai\",\"language\":\"zh-CN\","
    "\"volume\":30,\"brightness\":70,\"dnd\":{\"enabled\":true,\"startMinute\":1320,\"endMinute\":420,"
    "\"allowCritical\":true},\"message\":{\"queueDepth\":8,\"durationMs\":8000}}";
int main(void)
{
    assert(pet_config_init() == ESP_OK);
    pet_config_snapshot_t s;
    assert(pet_config_snapshot(&s) == ESP_OK && s.revision == 0 && s.writable);
    assert(s.config.volume == 25);
    assert(pet_config_apply_json(json, strlen(json), 0) == ESP_OK);
    assert(pet_config_snapshot(&s) == ESP_OK && s.revision == 1 && s.config.brightness == 70);
    unsigned previous_writes = writes;
    pet_config_t unsupported=s.config;unsupported.language=PET_CFG_LANGUAGE_EN_US;
    assert(pet_config_apply(&unsupported,1)==ESP_ERR_NOT_SUPPORTED&&writes==previous_writes);
    char english[sizeof(json)];memcpy(english,json,sizeof(json));
    memcpy(strstr(english,"zh-CN"),"en-US",5);
    assert(pet_config_apply_json(english,strlen(english),1)==ESP_ERR_NOT_SUPPORTED&&writes==previous_writes);
    pet_config_t timezone=s.config;
    strcpy(timezone.timezone,"UTC-12:30");
    assert(pet_config_validate(&timezone)==ESP_ERR_INVALID_ARG);
    strcpy(timezone.timezone,"UTC-12:00");
    assert(pet_config_validate(&timezone)==ESP_OK);
    assert(pet_config_apply_json(json, strlen(json), 1) == ESP_OK && writes == previous_writes);
    assert(pet_config_apply_json(json, strlen(json), 0) == ESP_ERR_INVALID_STATE);
    char invalid[768]; snprintf(invalid, sizeof(invalid), "%.*s,\"volume\":101}", (int)strlen(json)-1, json);
    assert(pet_config_apply_json(invalid, strlen(invalid), 1) == ESP_ERR_INVALID_ARG);
    assert(writes == previous_writes);
    assert(pet_config_dnd_active(&s.config, 57600u, true)); /* UTC 16:00，UTC+8 为午夜。 */
    assert(pet_config_dnd_active(&s.config, 0, false)); /* 未对时不放开定时隐私。 */
    pet_config_close();
    assert(pet_config_init()==ESP_OK);
    char response[768];
    assert(pet_config_command("PET_SETTINGS_GET",16,response,sizeof(response))==ESP_OK);
    assert(strstr(response,"\"revision\":1")&&strstr(response,"\"writable\":true"));
    assert(pet_config_command("PET_SETTINGS_GET",16,response,32)!=ESP_OK);
    const char provision[]="PET_PROVISION {\"expectedRevision\":0,\"credentials\":{\"net\":{\"wifi_ssid\":\"test-only\",\"wifi_pass\":\"dummy-pass\"},\"nexus\":{\"device_id\":\"test-device\",\"mqtt_uri\":\"mqtts://example.test:8883\",\"mqtt_user\":\"test-device\",\"mqtt_pass\":\"dummy-token\"}}}";
    assert(pet_config_command(provision,strlen(provision),response,sizeof(response))==ESP_OK);
    assert(strstr(response,"\"ok\":true")&&!strstr(response,"dummy"));
    uint32_t revision; assert(pet_provision_revision(&revision)==ESP_OK&&revision==1);
    char value[65];
    assert(pet_provision_load_str("nexus","device_id",value,sizeof(value))==ESP_OK&&!strcmp(value,"test-device"));
    assert(pet_provision_load_str("xiaozhi","ws_token",value,sizeof(value))==ESP_ERR_INVALID_STATE);
    assert(pet_config_command(provision,strlen(provision),response,sizeof(response))==ESP_ERR_INVALID_STATE);
    pet_config_close();
    assert(pet_config_init() == ESP_OK);
    assert(pet_config_snapshot(&s) == ESP_OK && s.revision == 1 && s.config.volume == 30);
    char exported[768]; size_t size;
    assert(pet_config_export_json(exported, sizeof(exported), &size) == ESP_OK);
    assert(strstr(exported, "\"volume\":30") && !strstr(exported, "token"));
    pet_config_t changed = s.config; changed.volume = 15;
    fail_write = true;
    assert(pet_config_apply(&changed, 1) != ESP_OK);
    assert(pet_config_snapshot(&s) == ESP_OK && s.config.volume == 30);
    fail_write = false;
    assert(pet_config_apply(&changed, 1) == ESP_OK);
    changed.volume=45;char settings[768],remote[1200],reply[1200];size_t settings_bytes;
    assert(pet_config_format_json(&changed,settings,sizeof(settings),&settings_bytes)==ESP_OK);
    snprintf(remote,sizeof(remote),"{\"schemaVersion\":1,\"deviceId\":\"test-device\",\"requestId\":\"settings-1\",\"expectedRevision\":2,\"issuedAt\":100,\"expiresAt\":160,\"config\":%s}",settings);
    assert(pet_config_remote("test-device",remote,strlen(remote),true,120,reply,sizeof(reply))!=ESP_OK);
    assert(pet_config_remote("other-device",remote,strlen(remote),false,120,reply,sizeof(reply))!=ESP_OK);
    assert(pet_config_remote("test-device",remote,strlen(remote),false,170,reply,sizeof(reply))!=ESP_OK);
    assert(pet_config_remote("test-device",remote,strlen(remote),false,120,reply,sizeof(reply))==ESP_OK);
    assert(pet_config_snapshot(&s)==ESP_OK && s.revision==3&&s.config.volume==45);
    assert(pet_config_remote("test-device",remote,strlen(remote),false,120,reply,sizeof(reply))==ESP_OK);
    assert(strstr(reply,"settings-1")&&strstr(reply,"applied"));
    assert(pet_config_worker_start()==ESP_OK);
    pet_config_t local_privacy=s.config;local_privacy.muted=true;uint32_t token;
    assert(pet_config_submit(&local_privacy,3,&token)==ESP_OK);
    changed=s.config;changed.volume=10;
    assert(pet_config_format_json(&changed,settings,sizeof(settings),&settings_bytes)==ESP_OK);
    snprintf(remote,sizeof(remote),"{\"schemaVersion\":1,\"deviceId\":\"test-device\",\"requestId\":\"racing-settings\",\"expectedRevision\":3,\"issuedAt\":100,\"expiresAt\":160,\"config\":%s}",settings);
    previous_writes=writes;
    assert(pet_config_remote("test-device",remote,strlen(remote),false,120,reply,sizeof(reply))!=ESP_OK);
    assert(writes==previous_writes);
    pthread_t thread;assert(!pthread_create(&thread,NULL,run_settings,NULL));
    esp_err_t completed=ESP_FAIL;
    unsigned waiting=0;
    while(pet_config_result(token,&completed)==ESP_ERR_TIMEOUT) {
        assert(waiting++<1000);struct timespec delay={.tv_nsec=1000000};nanosleep(&delay,NULL);
    }
    assert(completed==ESP_OK);
    assert(pet_config_snapshot(&s)==ESP_OK&&s.revision==4&&s.config.muted&&s.config.volume==45);
    queue_send_failed=true;
    assert(pet_config_submit(&s.config,4,&token)!=ESP_OK);
    assert(pet_config_apply(&s.config,4)==ESP_OK); /* An enqueue failure must release its reservation. */
    pthread_mutex_lock(&settings_queue->lock);settings_queue->stop=true;pthread_cond_signal(&settings_queue->ready);pthread_mutex_unlock(&settings_queue->lock);
    assert(!pthread_join(thread,NULL));vQueueDelete(settings_queue);
    pet_config_close(); saved[0] ^= 1;
    assert(pet_config_init() != ESP_OK);
    assert(pet_config_snapshot(&s) == ESP_OK && !s.writable);
    assert(pet_config_apply(&changed, s.revision) == ESP_ERR_INVALID_STATE);
    pet_config_close();
    puts("Config: strict validation, atomic persistence, revision, restart and privacy passed");
    return 0;
}
