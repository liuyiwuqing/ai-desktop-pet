#include "svc_config/api.h"
#include "config_pending.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

typedef struct { pet_config_t config; uint32_t revision, token; } request_t;
static QueueHandle_t queue;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t completed;
static bool busy;
static esp_err_t last_result;
static void worker(void *arg)
{
    (void)arg;
    request_t r;
    for (;;) if (xQueueReceive(queue, &r, portMAX_DELAY) == pdTRUE) {
        esp_err_t error = pet_config_apply_reserved(&r.config, r.revision,r.token);
        portENTER_CRITICAL(&lock);
        completed = r.token; last_result = error; busy = false;
        portEXIT_CRITICAL(&lock);
    }
}
esp_err_t pet_config_worker_start(void)
{
    if (queue) return ESP_ERR_INVALID_STATE;
    queue = xQueueCreate(1, sizeof(request_t));
    if (!queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(worker, "pet_settings", 6144, NULL, 2, NULL) != pdPASS) {
        vQueueDelete(queue); queue = NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t pet_config_submit(const pet_config_t *config, uint32_t revision, uint32_t *token)
{
    if (!token || pet_config_validate(config) != ESP_OK) return ESP_ERR_INVALID_ARG;
    if (!queue) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&lock);
    if (busy) { portEXIT_CRITICAL(&lock); return ESP_ERR_TIMEOUT; }
    busy = true;
    portEXIT_CRITICAL(&lock);
    uint32_t next;
    esp_err_t reserved=pet_config_reserve(revision,&next);
    if(reserved!=ESP_OK) {
        portENTER_CRITICAL(&lock);busy=false;portEXIT_CRITICAL(&lock);return reserved;
    }
    const request_t r = {*config, revision, next};
    if (xQueueSend(queue, &r, 0) != pdTRUE) {
        pet_config_cancel_reservation(next);
        portENTER_CRITICAL(&lock); busy = false; portEXIT_CRITICAL(&lock);
        return ESP_ERR_TIMEOUT;
    }
    *token = next; return ESP_OK;
}
esp_err_t pet_config_result(uint32_t token, esp_err_t *result)
{
    if (!token || !result) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    esp_err_t error = token > completed ? ESP_ERR_TIMEOUT : token == completed ? ESP_OK : ESP_ERR_NOT_FOUND;
    if (error == ESP_OK) *result = last_result;
    portEXIT_CRITICAL(&lock);
    return error;
}
