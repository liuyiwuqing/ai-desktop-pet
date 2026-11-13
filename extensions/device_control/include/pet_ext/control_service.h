#pragma once
#include "esp_err.h"
#include "pet_ext/command.h"
typedef struct {
    bool configured,awaiting_confirmation,busy;
    uint64_t next_request_id,confirmation_nonce,expires_ms;
    pet_command_request_t request;
    pet_command_result_t result;
    uint64_t last_job_id;
    pet_command_status_t last_job_status;
} pet_control_view_t;
typedef struct {char applications[16][41],projects[16][41];size_t application_count,project_count;} pet_control_catalog_t;
esp_err_t pet_control_service_start(const char *device_id,
    esp_err_t (*load_str)(const char *,const char *,char *,size_t));
esp_err_t pet_control_service_submit(const pet_command_request_t *request);
/* nonce 只供可信显示/维护入口使用；云端工具永不返回该值。 */
esp_err_t pet_control_service_confirm(uint64_t id,uint64_t nonce,bool approved);
esp_err_t pet_control_service_reconcile(uint64_t id);
esp_err_t pet_control_service_snapshot(pet_control_view_t *out);
esp_err_t pet_control_service_catalog(pet_control_catalog_t *out);
