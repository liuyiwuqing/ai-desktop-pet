#pragma once
#include <stdatomic.h>
#include "svc_setup/api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "esp_netif.h"

typedef enum { SETUP_JOB_WIFI, SETUP_JOB_SERVICES, SETUP_JOB_ACTIVATE, SETUP_JOB_PORTAL, SETUP_JOB_NETWORK } setup_job_kind_t;
typedef struct {
    setup_job_kind_t kind;
    uint32_t revision;
    char ssid[33], password[65];
    char *json;
    size_t bytes;
    pet_setup_portal_t portal;
    pet_setup_netcfg_t netcfg;
    bool restart;
} setup_job_t;

typedef struct {
    SemaphoreHandle_t lock;
    QueueHandle_t jobs;
    EventGroupHandle_t network_events;
    httpd_handle_t http;
    esp_netif_t *ap, *sta;
    pet_setup_snapshot_t snapshot;
    pet_setup_portal_t portal;
    pet_setup_netcfg_t netcfg;
    char session[33];
    int64_t deadline_us;
    bool sntp_started;
    atomic_bool busy, trying, leaving;
} setup_runtime_t;

#define SETUP_CONNECTED BIT0
#define SETUP_DISCONNECTED BIT1
void setup_wipe(void *bytes,size_t count);
void setup_state(setup_runtime_t *,pet_setup_state_t,esp_err_t);
bool setup_cancelled(setup_runtime_t *);
esp_err_t setup_portal_load(pet_setup_portal_t *);
esp_err_t setup_portal_save(const pet_setup_portal_t *);
esp_err_t setup_network_start(setup_runtime_t *);
esp_err_t setup_network_connect(setup_runtime_t *,const char *,const char *);
esp_err_t setup_http_start(setup_runtime_t *);
esp_err_t setup_activation_run(setup_runtime_t *);
esp_err_t setup_boot_save(uint32_t mode);
