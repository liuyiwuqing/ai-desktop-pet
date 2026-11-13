#include "sdkconfig.h"
#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo_service.h"
#include <string.h>
#include <stdatomic.h>
#include "pet/storage.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs.h"
typedef struct {pet_servo_action_t action;uint64_t id;int16_t angle;uint16_t duration;bool arm;} job_t;
static QueueHandle_t jobs;
static _Atomic bool emergency,stop_requested;
static portMUX_TYPE view_lock=portMUX_INITIALIZER_UNLOCKED;
static pet_servo_service_view_t view;
static pet_servo_engine_t engine;
static bool attached,storage_uncertain;
static uint64_t watermark;
static bool (*gpio_allowed)(int);
static bool attach(void *ctx,const pet_servo_profile_t *p,size_t count)
{
    (void)ctx;
    if(count!=1||!gpio_allowed||!gpio_allowed(p->gpio))return false;
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_14_BIT,
        .timer_num=LEDC_TIMER_1,.freq_hz=50,.clk_cfg=LEDC_AUTO_CLK};
    if(ledc_timer_config(&timer)!=ESP_OK)return false;
    ledc_channel_config_t channel={.gpio_num=p->gpio,.speed_mode=LEDC_LOW_SPEED_MODE,
        .channel=LEDC_CHANNEL_1,.timer_sel=LEDC_TIMER_1,.duty=0,.hpoint=0};
    if(ledc_channel_config(&channel)!=ESP_OK)return false;
    attached=true;return true;
}
static bool pulse(void *ctx,uint8_t id,uint16_t us)
{
    (void)ctx;
    if(id!=1||!attached)return false;
    uint32_t duty=(uint32_t)us*16384u/20000u;
    return ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,duty)==ESP_OK&&
        ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1)==ESP_OK;
}
static bool detach(void *ctx)
{
    (void)ctx;
    if(!attached)return true;
    if(ledc_stop(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_1,0)!=ESP_OK)return false;
    attached=false;return true;
}
static bool reserve(void *ctx,uint64_t id)
{
    (void)ctx;
    if(storage_uncertain||id<=watermark)return false;
    uint8_t bytes[8],check[8];size_t length=0;
    for(unsigned i=0;i<8;++i)bytes[i]=(uint8_t)(id>>(i*8));
    (void)pet_storage_save("pet_servo","watermark",bytes,sizeof(bytes));
    if(pet_storage_load("pet_servo","watermark",check,sizeof(check),&length)!=ESP_OK||length!=8||memcmp(bytes,check,8)) {
        storage_uncertain=true;return false;
    }
    watermark=id;return true;
}
static void publish(uint64_t id,pet_servo_result_t result)
{
    pet_servo_state_t state;pet_servo_get_state(&engine,1,&state);
    portENTER_CRITICAL(&view_lock);view=(pet_servo_service_view_t){id,result,state,true};portEXIT_CRITICAL(&view_lock);
}
static void owner(void *arg)
{
    (void)arg;
    for(;;) {
        if(atomic_exchange(&emergency,false)) {
            job_t discarded;while(xQueueReceive(jobs,&discarded,0)==pdTRUE){}
            publish(view.request_id,pet_servo_stop(&engine,true));
        } else if(atomic_exchange(&stop_requested,false)) {
            job_t discarded;while(xQueueReceive(jobs,&discarded,0)==pdTRUE){}
            publish(view.request_id,pet_servo_stop(&engine,false));
        }
        job_t job;
        if(xQueueReceive(jobs,&job,0)==pdTRUE) {
            pet_servo_result_t result;
            uint64_t now=(uint64_t)esp_timer_get_time()/1000;
            if(job.arm)result=pet_servo_arm(&engine,true);
            else if(job.action==PET_SERVO_CMD_WAG||job.action==PET_SERVO_CMD_NOD) {
                int16_t center=engine.profiles[0].safe_position,delta=job.action==PET_SERVO_CMD_WAG?20:10;
                int16_t low=center-delta,high=center+delta;
                if(low<engine.profiles[0].min_angle)low=engine.profiles[0].min_angle;
                if(high>engine.profiles[0].max_angle)high=engine.profiles[0].max_angle;
                const pet_servo_keyframe_t frames[]={{high,500,100},{low,500,100},{center,500,0}};
                result=pet_servo_sequence(&engine,job.id,1,frames,3,now,false);
            } else result=pet_servo_move(&engine,job.id,1,
                job.action==PET_SERVO_CMD_HOME?engine.profiles[0].safe_position:job.angle,job.duration,now,false);
            publish(job.id,result);
        }
        pet_servo_result_t result=pet_servo_tick(&engine,(uint64_t)esp_timer_get_time()/1000);
        publish(view.request_id,result==PET_SERVO_OK?view.result:result);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
esp_err_t pet_servo_service_start(bool (*available)(int))
{
    if(jobs)return ESP_ERR_INVALID_STATE;
    if(!available||!available(CONFIG_PET_SERVO_GPIO))return ESP_ERR_INVALID_ARG;
    gpio_allowed=available;
    const pet_servo_profile_t profile={1,CONFIG_PET_SERVO_GPIO,500,2500,
        CONFIG_PET_SERVO_MIN_ANGLE,CONFIG_PET_SERVO_MAX_ANGLE,CONFIG_PET_SERVO_SAFE_ANGLE,CONFIG_PET_SERVO_MAX_SPEED,false};
    uint8_t stored[8];size_t length=0;
    esp_err_t error=pet_storage_init();if(error!=ESP_OK)return error;
    error=pet_storage_load("pet_servo","watermark",stored,sizeof(stored),&length);
    if(error==ESP_OK) {if(length!=8)return ESP_ERR_INVALID_STATE;for(unsigned i=0;i<8;++i)watermark|=(uint64_t)stored[i]<<(i*8);}
    else if(error!=ESP_ERR_NVS_NOT_FOUND)return error;
    const pet_actuator_port_t port={NULL,attach,pulse,detach,reserve};
    if(pet_servo_engine_init(&engine,&profile,1,&port,watermark)!=PET_SERVO_OK)return ESP_ERR_INVALID_ARG;
    jobs=xQueueCreate(4,sizeof(job_t));if(!jobs)return ESP_ERR_NO_MEM;
    publish(0,PET_SERVO_DISARMED);
    if(xTaskCreate(owner,"pet_servo",4096,NULL,4,NULL)!=pdPASS){vQueueDelete(jobs);jobs=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t pet_servo_service_arm(void)
{if(!jobs)return ESP_ERR_INVALID_STATE;const job_t job={.arm=true};return xQueueSend(jobs,&job,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;}
esp_err_t pet_servo_service_submit(uint64_t request,pet_servo_action_t action,int16_t angle,uint16_t duration)
{
    if(!jobs)return ESP_ERR_INVALID_STATE;
    if(!request||(unsigned)action>PET_SERVO_CMD_NOD)return ESP_ERR_INVALID_ARG;
    const job_t job={.action=action,.id=request,.angle=angle,.duration=duration};
    return xQueueSend(jobs,&job,0)==pdTRUE?ESP_OK:ESP_ERR_TIMEOUT;
}
esp_err_t pet_servo_service_stop(bool urgent)
{if(!jobs)return ESP_ERR_INVALID_STATE;if(urgent)atomic_store(&emergency,true);else atomic_store(&stop_requested,true);return ESP_OK;}
esp_err_t pet_servo_service_snapshot(pet_servo_service_view_t *out)
{if(!out)return ESP_ERR_INVALID_ARG;portENTER_CRITICAL(&view_lock);*out=view;portEXIT_CRITICAL(&view_lock);return jobs?ESP_OK:ESP_ERR_INVALID_STATE;}
#endif
