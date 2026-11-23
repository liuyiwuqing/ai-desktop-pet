#include "ai_startup.h"
#include "sdkconfig.h"

#if CONFIG_PET_AI_XIAOZHI_ON_BOOT
#include <string.h>

#include "nvs.h"
#include "pet/storage.h"
#include "svc_ai_agent/wss.h"
#if CONFIG_PET_AI_ESP_SR_ENABLED
#include "svc_ai_agent/sr.h"
static pet_ai_sr_t speech_frontend;
#endif
#if CONFIG_PET_AI_DSP_ENABLED
#include "svc_ai_agent/dsp.h"
static pet_ai_dsp_t dsp_frontend;
#endif

/* 不占用 main 的 4 KB 栈；WSS 复制后清掉临时凭据，启动过程不写 NVS。 */
static struct {
    char uri[PET_XZ_WSS_URI_BYTES + 1];
    char token[PET_XZ_WSS_TOKEN_BYTES + 1];
    char device_id[PET_XZ_WSS_ID_BYTES + 1];
    char client_id[PET_XZ_WSS_ID_BYTES + 1];
    char ca[PET_XZ_WSS_CA_BYTES + 1];
} credentials;

static void clear_credentials(void)
{
    volatile unsigned char *bytes = (volatile unsigned char *)&credentials;
    for (size_t i = 0; i < sizeof(credentials); ++i) bytes[i] = 0;
}

esp_err_t app_ai_start(uint8_t volume, uint8_t sfx_volume, bool muted, bool dnd,
    esp_err_t (*load_str)(const char *, const char *, char *, size_t),
    void (*completed)(uint32_t))
{
    if (volume > 100 || sfx_volume > 100 || !load_str) return ESP_ERR_INVALID_ARG;
    const struct {
        const char *key;
        char *out;
        size_t capacity;
    } required[] = {
        {"ws_uri", credentials.uri, sizeof(credentials.uri)},
        {"ws_token", credentials.token, sizeof(credentials.token)},
        {"device_id", credentials.device_id, sizeof(credentials.device_id)},
        {"client_id", credentials.client_id, sizeof(credentials.client_id)},
    };
    const char *ca = credentials.ca;
    clear_credentials();
    esp_err_t error = pet_storage_init();
    for (size_t i = 0; error == ESP_OK && i < sizeof(required) / sizeof(required[0]); ++i)
        error = load_str("xiaozhi", required[i].key, required[i].out, required[i].capacity);
    if (error == ESP_OK) {
        error = load_str("xiaozhi", "ws_ca", credentials.ca, sizeof(credentials.ca));
        if (error == ESP_ERR_NVS_NOT_FOUND) {
            ca = NULL;
            error = ESP_OK;
        }
    }
    if (error == ESP_OK) {
        const pet_xz_wss_config_t config = {
            .uri = credentials.uri, .token = credentials.token,
            .device_id = credentials.device_id, .client_id = credentials.client_id,
            .ca_cert_pem = ca,
        };
        pet_ai_agent_config_t ai = {.audio = pet_bsp_audio(), .volume_pct = volume,
            .sfx_volume_pct = sfx_volume,
            .initial_policy = {.muted = muted, .dnd = dnd},.on_completed=completed};
#if CONFIG_PET_AI_ESP_SR_ENABLED
        ai.frontend = pet_ai_sr_frontend(&speech_frontend);
#endif
#if CONFIG_PET_AI_DSP_ENABLED
        bool request_aec=false;
#if CONFIG_PET_AI_AEC_REFERENCE_ENABLED
        request_aec=true;
#endif
        ai.frontend=pet_ai_dsp_frontend(&dsp_frontend,ai.frontend,request_aec);
#endif
        error = pet_xz_wss_start(&config, &ai.transport);
        if (error == ESP_OK) error = pet_ai_agent_start(&ai);
    }
    clear_credentials();
    return error;
}
#endif
