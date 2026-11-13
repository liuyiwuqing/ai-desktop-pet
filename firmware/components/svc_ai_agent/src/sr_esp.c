#include "svc_ai_agent/sr.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_vadn_models.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "psa/crypto.h"
#include "pet_sr_model_info.h"

#define MAX_MODEL_SAMPLES 960u
#define MODEL_LIMIT_BYTES (960u * 1024u)
#define WAKE_MODEL "wn9_xiaoaitongxue"
#define VAD_MODEL "vadnet1_medium"

typedef struct {
    srmodel_list_t *models;
    const esp_wn_iface_t *wake;
    const esp_vadn_iface_t *vad;
    model_iface_data_t *wake_data, *vad_data;
    size_t wake_samples, vad_samples, fill;
    unsigned mode;
    uint64_t last_ms;
    bool have_time;
    pet_ai_activity_t activity;
    /* 唤醒和会话 VAD 互斥，共享一个有界缓冲；不保存调用方 PCM。 */
    int16_t pcm[MAX_MODEL_SAMPLES];
} sr_state_t;

bool pet_ai_sr_verify_models(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "model");
    if (partition == NULL || partition->size != 0x100000 ||
        PET_SR_MODEL_IMAGE_BYTES > MODEL_LIMIT_BYTES || PET_SR_MODEL_IMAGE_BYTES < 4)
        return false;
    const void *image = NULL;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(partition, 0, PET_SR_MODEL_IMAGE_BYTES,
                           ESP_PARTITION_MMAP_DATA, &image, &handle) != ESP_OK) return false;
    unsigned char digest[32];
    size_t size = 0;
    const bool valid = psa_crypto_init() == PSA_SUCCESS &&
        psa_hash_compute(PSA_ALG_SHA_256, image, PET_SR_MODEL_IMAGE_BYTES,
                         digest, sizeof(digest), &size) == PSA_SUCCESS &&
        size == sizeof(digest) && memcmp(digest, pet_sr_model_sha256, sizeof(digest)) == 0;
    esp_partition_munmap(handle);
    return valid;
}

static pet_ai_port_result_t close_frontend(void *context)
{
    pet_ai_sr_t *frontend = context;
    if (frontend == NULL) return PET_AI_PORT_INVALID;
    sr_state_t *state = frontend->state;
    if (state == NULL) return PET_AI_PORT_OK;
    if (state->vad_data != NULL) state->vad->destroy(state->vad_data);
    if (state->wake_data != NULL) state->wake->destroy(state->wake_data);
    /* 模型句柄先销毁，最后解除它们引用的 flash mmap。 */
    if (state->models != NULL) esp_srmodel_deinit(state->models);
    memset(state, 0, sizeof(*state));
    heap_caps_free(state);
    frontend->state = NULL;
    return PET_AI_PORT_OK;
}

static bool valid_samples(int samples)
{
    return samples >= 160 && samples <= (int)MAX_MODEL_SAMPLES && samples % 16 == 0;
}

static pet_ai_port_result_t open_frontend(void *context)
{
    pet_ai_sr_t *frontend = context;
    if (frontend == NULL || frontend->state != NULL) return PET_AI_PORT_INVALID;
    /* 先校验与当前固件配套的镜像，再让缺少长度检查的 SDK 解析模型头。 */
    if (!pet_ai_sr_verify_models()) return PET_AI_PORT_FAILED;
    sr_state_t *state = heap_caps_malloc(sizeof(*state), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (state == NULL) return PET_AI_PORT_FAILED;
    memset(state, 0, sizeof(*state));
    frontend->state = state;
    state->models = esp_srmodel_init("model");
    if (state->models == NULL || state->models->num != 2 || state->models->model_name == NULL)
        goto fail;
    const char *wake_name = NULL, *vad_name = NULL;
    for (int i = 0; i < state->models->num; ++i) {
        const char *name = state->models->model_name[i];
        if (name == NULL) goto fail;
        if (strcmp(name, WAKE_MODEL) == 0) wake_name = name;
        if (strcmp(name, VAD_MODEL) == 0) vad_name = name;
    }
    if (wake_name == NULL || vad_name == NULL) goto fail;
    state->wake = esp_wn_handle_from_name(wake_name);
    state->vad = esp_vadn_handle_from_name(vad_name);
    if (state->wake == NULL || state->vad == NULL || !state->wake->create || !state->vad->create ||
        !state->wake->detect || !state->vad->detect || !state->wake->clean || !state->vad->clean ||
        !state->wake->destroy || !state->vad->destroy || !state->wake->get_samp_chunksize ||
        !state->vad->get_samp_chunksize || !state->wake->get_samp_rate || !state->vad->get_samp_rate ||
        !state->wake->get_channel_num || !state->vad->get_channel_num) goto fail;
    state->wake_data = state->wake->create(wake_name, DET_MODE_95);
    /* 不叠加 SDK 的端点延迟；300 ms 端点与最短语音仍由 session 统一裁决。 */
    state->vad_data = state->vad->create(vad_name, VAD_MODE_1, 1, 32, 32);
    if (state->wake_data == NULL || state->vad_data == NULL) goto fail;
    const int wake_samples = state->wake->get_samp_chunksize(state->wake_data);
    const int vad_samples = state->vad->get_samp_chunksize(state->vad_data);
    if (!valid_samples(wake_samples) || !valid_samples(vad_samples) ||
        state->wake->get_samp_rate(state->wake_data) != PET_AUDIO_CAPTURE_RATE ||
        state->vad->get_samp_rate(state->vad_data) != PET_AUDIO_CAPTURE_RATE ||
        state->wake->get_channel_num(state->wake_data) != 1 ||
        state->vad->get_channel_num(state->vad_data) != 1) goto fail;
    state->wake_samples = (size_t)wake_samples;
    state->vad_samples = (size_t)vad_samples;
    state->activity = PET_AI_ACTIVITY_SILENCE;
    return PET_AI_PORT_OK;
fail:
    (void)close_frontend(frontend);
    return PET_AI_PORT_FAILED;
}

static pet_ai_port_result_t reset_frontend(void *context)
{
    pet_ai_sr_t *frontend = context;
    if (frontend == NULL || frontend->state == NULL) return PET_AI_PORT_INVALID;
    sr_state_t *state = frontend->state;
    state->wake->clean(state->wake_data);
    state->vad->clean(state->vad_data);
    state->fill = state->mode = 0;
    state->have_time = false;
    state->activity = PET_AI_ACTIVITY_SILENCE;
    memset(state->pcm, 0, sizeof(state->pcm));
    return PET_AI_PORT_OK;
}

static pet_ai_port_result_t process(pet_ai_sr_t *frontend, const int16_t *pcm, size_t samples,
                                    uint64_t now, bool wake, bool *detected,
                                    pet_ai_activity_t *activity)
{
    if (frontend == NULL || frontend->state == NULL || pcm == NULL ||
        samples != PET_AUDIO_CAPTURE_SAMPLES) return PET_AI_PORT_INVALID;
    sr_state_t *state = frontend->state;
    if (state->have_time && now <= state->last_ms) return PET_AI_PORT_INVALID;
    const unsigned mode = wake ? 1 : 2;
    if (mode != state->mode || (state->have_time && now - state->last_ms > PET_AI_FRAME_GAP_MS)) {
        (void)reset_frontend(frontend);
        state->mode = mode;
    }
    state->last_ms = now;
    state->have_time = true;
    const size_t chunk = wake ? state->wake_samples : state->vad_samples;
    bool speech = false, classified = false;
    while (samples > 0) {
        const size_t remaining = chunk - state->fill;
        const size_t copy = samples < remaining ? samples : remaining;
        memcpy(state->pcm + state->fill, pcm, copy * sizeof(*pcm));
        state->fill += copy;
        pcm += copy;
        samples -= copy;
        if (state->fill != chunk) continue;
        if (wake) {
            const wakenet_state_t result = state->wake->detect(state->wake_data, state->pcm);
            if (result != WAKENET_NO_DETECT && result != WAKENET_DETECTED &&
                result != WAKENET_CHANNEL_VERIFIED) return PET_AI_PORT_FAILED;
            *detected |= result == WAKENET_DETECTED;
        } else {
            const vad_state_t result = state->vad->detect(state->vad_data, state->pcm);
            if (result != VAD_SPEECH && result != VAD_SILENCE) return PET_AI_PORT_FAILED;
            speech |= result == VAD_SPEECH;
            classified = true;
        }
        state->fill = 0;
        memset(state->pcm, 0, sizeof(state->pcm));
    }
    if (!wake) {
        if (classified) state->activity = speech ? PET_AI_ACTIVITY_SPEECH : PET_AI_ACTIVITY_SILENCE;
        *activity = state->activity;
    }
    return PET_AI_PORT_OK;
}

static pet_ai_port_result_t detect_wake(void *context, const int16_t *pcm, size_t samples,
                                       uint64_t now, bool *detected)
{
    if (detected == NULL) return PET_AI_PORT_INVALID;
    *detected = false;
    return process(context, pcm, samples, now, true, detected, NULL);
}

static pet_ai_port_result_t classify_vad(void *context, const int16_t *pcm, size_t samples,
                                        uint64_t now, pet_ai_activity_t *activity)
{
    if (activity == NULL) return PET_AI_PORT_INVALID;
    *activity = PET_AI_ACTIVITY_SILENCE;
    return process(context, pcm, samples, now, false, NULL, activity);
}

pet_ai_runtime_frontend_t pet_ai_sr_frontend(pet_ai_sr_t *frontend)
{
    return (pet_ai_runtime_frontend_t){
        .context = frontend, .open = open_frontend, .close = close_frontend,
        .reset = reset_frontend, .detect_wake = detect_wake, .classify_vad = classify_vad,
    };
}
