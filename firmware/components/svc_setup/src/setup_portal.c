#include "setup_internal.h"
#include "setup_policy.h"
#include "pet/storage.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

#define PORTAL_NAMESPACE "pet_setup"
#define PORTAL_KEY "portal_v1"
#define PORTAL_JSON_MAX 512u

/* 偏好与凭据分开存放：热点名称/口令不参与 provision bundle 的 revision/CAS。 */
esp_err_t setup_portal_load(pet_setup_portal_t *out)
{
    if(!out) return ESP_ERR_INVALID_ARG;
    pet_setup_portal_default(out);
    char json[PORTAL_JSON_MAX]={0};
    esp_err_t err=pet_storage_load_str(PORTAL_NAMESPACE,PORTAL_KEY,json,sizeof(json));
    if(err==ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
    if(err!=ESP_OK) {setup_wipe(json,sizeof(json));return err;}
    size_t bytes=strlen(json);
    bool parsed=bytes<=PORTAL_JSON_MAX&&pet_setup_portal_parse(json,bytes,out);
    setup_wipe(json,sizeof(json));
    if(!parsed) {
        /* 损坏偏好回落默认热点，不阻断配网，也不清除已保存的家庭凭据。 */
        pet_setup_portal_default(out);
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

esp_err_t setup_portal_save(const pet_setup_portal_t *portal)
{
    if(!portal||!pet_setup_portal_valid(portal)) return ESP_ERR_INVALID_ARG;
    char json[PORTAL_JSON_MAX]={0},readback[PORTAL_JSON_MAX]={0};
    esp_err_t err=ESP_ERR_NO_MEM;
    if(!pet_setup_portal_format(portal,json,sizeof(json))) return err;
    err=pet_storage_save_str(PORTAL_NAMESPACE,PORTAL_KEY,json);
    if(err==ESP_OK) {
        esp_err_t checked=pet_storage_load_str(PORTAL_NAMESPACE,PORTAL_KEY,readback,sizeof(readback));
        if(checked!=ESP_OK||strcmp(readback,json)) err=ESP_FAIL;
    }
    setup_wipe(json,sizeof(json));setup_wipe(readback,sizeof(readback));
    if(err!=ESP_OK) ESP_LOGW("pet_setup","portal preferences not saved: %s",esp_err_to_name(err));
    return err;
}
