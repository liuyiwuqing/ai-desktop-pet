#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pet/event_bus.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/* Run the real dispatcher until its next idle wait. The SDK boundary models
 * Queue Set notifications independently of member reads, as FreeRTOS does. */
typedef struct queue {
    unsigned capacity, size, head, count;
    struct queue *set;
    unsigned char data[];
} queue_t;
static TaskFunction_t worker;
static void *worker_context;
static unsigned task_count, notifications, delivered, trace[64];
static uint32_t sequences[64];
static bool inject_on_wait;
static jmp_buf idle;

QueueHandle_t xQueueCreate(unsigned length,unsigned size)
{
    queue_t *q=calloc(1,sizeof(*q)+(size_t)length*size);
    assert(q);q->capacity=length;q->size=size;return q;
}
void vQueueDelete(QueueHandle_t q){free(q);}
int xQueueSend(QueueHandle_t handle,const void *value,TickType_t timeout)
{
    queue_t *q=handle;assert(q&&value&&timeout==0);
    if(q->count==q->capacity)return 0;
    memcpy(q->data+(q->head+q->count)%q->capacity*q->size,value,q->size);++q->count;
    if(q->set)assert(xQueueSend(q->set,&handle,0)==pdTRUE);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle,void *value,TickType_t timeout)
{
    queue_t *q=handle;assert(q&&value&&timeout==0);
    if(!q->count)return 0;
    memcpy(value,q->data+q->head*q->size,q->size);
    q->head=(q->head+1)%q->capacity;--q->count;return pdTRUE;
}
QueueSetHandle_t xQueueCreateSet(unsigned length){return xQueueCreate(length,sizeof(void *));}
int xQueueAddToSet(QueueHandle_t handle,QueueSetHandle_t set)
{queue_t *q=handle;assert(!q->set);q->set=set;return pdPASS;}
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set,TickType_t timeout)
{
    assert(timeout==portMAX_DELAY);void *member=NULL;
    if(!xQueueReceive(set,&member,0))longjmp(idle,1);
    return member;
}
int xTaskCreate(TaskFunction_t function,const char *name,uint32_t stack,void *context,
                unsigned priority,TaskHandle_t *task)
{
    (void)name;(void)stack;(void)priority;
    assert(!worker);worker=function;worker_context=context;*task=&worker;++task_count;return pdPASS;
}
int xTaskNotifyGive(TaskHandle_t task){assert(task==&worker);++notifications;return pdPASS;}
static esp_err_t publish(unsigned value,pet_event_priority_t priority)
{
    pet_event_t event;pet_event_prepare(&event,PET_EVT_BOOT,PET_EVENT_SOURCE_APP,priority);
    event.correlation_id=value;return pet_event_bus_publish(&event);
}
uint32_t ulTaskNotifyTake(int clear,TickType_t timeout)
{
    assert(clear==pdTRUE&&timeout==portMAX_DELAY);
    if(inject_on_wait) {
        inject_on_wait=false;
        assert(publish(99,PET_EVENT_PRIORITY_CRITICAL)==ESP_OK);
    }
    if(!notifications)longjmp(idle,1);
    uint32_t value=notifications;notifications=0;return value;
}
static void handle(const pet_event_t *event)
{
    assert(delivered<64);trace[delivered]=event->correlation_id;
    sequences[delivered++]=event->sequence;
}
static void pump(void)
{
    if(!setjmp(idle)) {
        worker(worker_context);
        assert(!"event dispatcher returned instead of waiting for more events");
    }
}
int main(void)
{
    assert(publish(0,PET_EVENT_PRIORITY_NORMAL)==ESP_ERR_INVALID_STATE);
    assert(pet_event_bus_init()==ESP_OK&&pet_event_bus_init()==ESP_OK&&task_count==1);
    assert(pet_event_bus_subscribe(PET_EVT_BOOT,handle)==ESP_OK);
    assert(pet_event_bus_publish(NULL)==ESP_ERR_INVALID_ARG);
    assert(publish(1,PET_EVENT_PRIORITY_LOW)==ESP_OK);
    assert(publish(2,PET_EVENT_PRIORITY_NORMAL)==ESP_OK);
    assert(publish(3,PET_EVENT_PRIORITY_CRITICAL)==ESP_OK);
    assert(publish(4,PET_EVENT_PRIORITY_HIGH)==ESP_OK);
    assert(publish(5,PET_EVENT_PRIORITY_NORMAL)==ESP_OK);
    pump();
    const unsigned expected[]={3,4,2,5,1};
    assert(delivered==5&&!memcmp(trace,expected,sizeof(expected)));
    assert(sequences[2]<sequences[3]);
    for(unsigned round=0;round<500;++round) {
        delivered=0;pump(); /* Idle must not terminate the owner. */
        assert(publish(round,PET_EVENT_PRIORITY_NORMAL)==ESP_OK);pump();
        assert(delivered==1&&trace[0]==round);
    }
    delivered=0;inject_on_wait=true;pump();
    assert(delivered==1&&trace[0]==99); /* Arrival between empty scan and wait. */
    delivered=0;
    const unsigned depths[]={4,16,8,4};
    for(unsigned p=0;p<PET_EVENT_PRIORITY_COUNT;++p) {
        for(unsigned i=0;i<depths[p];++i)assert(publish(p*100+i,(pet_event_priority_t)p)==ESP_OK);
        assert(publish(999,(pet_event_priority_t)p)==ESP_ERR_NO_MEM);
        assert(pet_event_bus_dropped_by_priority((pet_event_priority_t)p)==1);
    }
    pump();assert(delivered==32&&pet_event_bus_dropped()==4);
    unsigned at=0;
    for(int p=PET_EVENT_PRIORITY_COUNT-1;p>=0;--p)
        for(unsigned i=0;i<depths[p];++i)assert(trace[at++]==(unsigned)p*100+i);
    puts("Event bus: priority/FIFO, repeated idle wake, boundary arrival and capacity passed");
    return 0;
}
