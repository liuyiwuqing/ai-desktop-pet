#include "app_recovery.h"
#include "sdkconfig.h"
#include "esp_system.h"
#if CONFIG_PET_RECOVERY_ENABLED
#include "recovery_engine.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
static RTC_NOINIT_ATTR pet_recovery_record_t record;
static pet_recovery_engine_t engine;
static bool started;
static uint32_t reason;
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
bool app_recovery_start(void)
{
    portENTER_CRITICAL(&lock);
    if(!started) {
        reason=esp_reset_reason();
        bool crash=reason==ESP_RST_PANIC||reason==ESP_RST_INT_WDT||reason==ESP_RST_TASK_WDT||
            reason==ESP_RST_WDT||reason==ESP_RST_CPU_LOCKUP;
        pet_recovery_begin(&engine,&record,crash,reason==ESP_RST_POWERON);started=true;
    }
    bool active=engine.active;portEXIT_CRITICAL(&lock);return active;
}
bool app_recovery_active(void)
{portENTER_CRITICAL(&lock);bool active=started&&engine.active;portEXIT_CRITICAL(&lock);return active;}
void app_recovery_tick(bool healthy)
{portENTER_CRITICAL(&lock);if(started)pet_recovery_tick(&engine,healthy,(uint64_t)esp_timer_get_time()/1000);portEXIT_CRITICAL(&lock);}
void app_recovery_clear(void)
{portENTER_CRITICAL(&lock);if(started)pet_recovery_clear(&engine);portEXIT_CRITICAL(&lock);}
bool app_recovery_claim_ui(void)
{portENTER_CRITICAL(&lock);bool allowed=started&&pet_recovery_claim_ui(&engine);portEXIT_CRITICAL(&lock);return allowed;}
void app_recovery_snapshot(app_recovery_snapshot_t *out)
{
    portENTER_CRITICAL(&lock);
    *out=(app_recovery_snapshot_t){.enabled=true,.active=started&&engine.active,
        .healthy=started&&engine.settled,.ui_attempted=started&&record.ui_attempted,
        .failures=started?record.failures:0,.reset_reason=reason};
    portEXIT_CRITICAL(&lock);
}
#else
bool app_recovery_start(void){return false;}
bool app_recovery_active(void){return false;}
void app_recovery_tick(bool healthy){(void)healthy;}
void app_recovery_clear(void){}
bool app_recovery_claim_ui(void){return false;}
void app_recovery_snapshot(app_recovery_snapshot_t *out)
{*out=(app_recovery_snapshot_t){.reset_reason=esp_reset_reason()};}
#endif
