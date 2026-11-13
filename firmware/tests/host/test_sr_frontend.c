#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "esp_wn_models.h"
#include "esp_vadn_models.h"
#include "model_path.h"
#include "psa/crypto.h"
#include "svc_ai_agent/sr.h"

static int wake_token, vad_token;
static char *model_names[] = {"wn9_xiaoaitongxue", "vadnet1_medium"};
static srmodel_list_t models = {.num = 2, .model_name = model_names};
static esp_partition_t partition = {.size = 0x100000};
static uint8_t flash[64];
static unsigned allocations, loaded, live, mappings, wake_detections;
static const char *failure = "";
static bool failing(const char *name) { return strcmp(failure, name) == 0; }

void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert(caps & MALLOC_CAP_SPIRAM);
    if (failing("alloc")) return NULL;
    ++allocations;
    return malloc(size);
}
void heap_caps_free(void *pointer) { assert(allocations); --allocations; free(pointer); }
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type,
    esp_partition_subtype_t subtype, const char *label)
{
    assert(type == ESP_PARTITION_TYPE_DATA && subtype == ESP_PARTITION_SUBTYPE_DATA_SPIFFS);
    assert(strcmp(label, "model") == 0);
    return failing("partition") ? NULL : &partition;
}
esp_err_t esp_partition_mmap(const esp_partition_t *part, size_t offset, size_t size,
    esp_partition_mmap_flag_t flags, const void **out, esp_partition_mmap_handle_t *handle)
{
    assert(part == &partition && offset == 0 && size == 64 && flags == ESP_PARTITION_MMAP_DATA);
    if (failing("map")) return ESP_FAIL;
    *out = flash; *handle = 1; ++mappings;
    return ESP_OK;
}
void esp_partition_munmap(esp_partition_mmap_handle_t handle) { assert(handle == 1 && mappings); --mappings; }
psa_status_t psa_crypto_init(void) { return failing("crypto-init") ? -1 : PSA_SUCCESS; }
psa_status_t psa_hash_compute(psa_algorithm_t algorithm, const uint8_t *input, size_t size,
    uint8_t *hash, size_t capacity, size_t *length)
{
    assert(algorithm == PSA_ALG_SHA_256 && input == flash && size == 64 && capacity == 32);
    memset(hash, 0, 32); hash[0] = failing("digest") ? 0x41 : 0x42;
    *length = failing("digest-size") ? 31 : 32;
    return failing("crypto-hash") ? -1 : PSA_SUCCESS;
}
srmodel_list_t *esp_srmodel_init(const char *label)
{
    assert(strcmp(label, "model") == 0);
    if (failing("load")) return NULL;
    ++loaded;
    return &models;
}
void esp_srmodel_deinit(srmodel_list_t *value) { assert(value == &models && loaded && live == 0); --loaded; }
static model_iface_data_t *wake_create(const void *name, det_mode_t mode)
{
    assert(strcmp(name, model_names[0]) == 0 && mode == DET_MODE_95);
    if (failing("wake-create")) return NULL;
    ++live;
    return (model_iface_data_t *)&wake_token;
}
static model_iface_data_t *create_vad_model(const void *name, vad_mode_t mode, int channels,
                                     int speech, int silence)
{
    assert(strcmp(name, model_names[1]) == 0 && mode == VAD_MODE_1 && channels == 1);
    assert(speech == 32 && silence == 32);
    if (failing("vad-create")) return NULL;
    ++live;
    return (model_iface_data_t *)&vad_token;
}
static int chunk(model_iface_data_t *value) { return failing("chunk") ? 1008 : value == (void *)&wake_token ? 480 : 512; }
static int rate(model_iface_data_t *value) { (void)value; return failing("rate") ? 8000 : 16000; }
static int channels(model_iface_data_t *value) { (void)value; return failing("channels") ? 2 : 1; }
static void clean(model_iface_data_t *value) { assert(value == (void *)&wake_token || value == (void *)&vad_token); }
static void destroy(model_iface_data_t *value) { clean(value); assert(live); --live; }
static wakenet_state_t detect(model_iface_data_t *value, int16_t *pcm)
{
    assert(value == (void *)&wake_token);
    for (unsigned i = 0; i < 480; ++i) assert(pcm[i] == (int16_t)(i + 1));
    ++wake_detections;
    return WAKENET_DETECTED;
}
static vad_state_t classify(model_iface_data_t *value, int16_t *pcm)
{
    assert(value == (void *)&vad_token && pcm);
    return VAD_SPEECH;
}
static const esp_wn_iface_t wake_iface = {
    .create = wake_create, .get_samp_chunksize = chunk, .get_samp_rate = rate,
    .get_channel_num = channels, .clean = clean, .destroy = destroy, .detect = detect,
};
static const esp_vadn_iface_t vad_iface = {
    .create = create_vad_model, .get_samp_chunksize = chunk, .get_samp_rate = rate,
    .get_channel_num = channels, .clean = clean, .destroy = destroy, .detect = classify,
};
const esp_wn_iface_t *esp_wn_handle_from_name(const char *name) { assert(strcmp(name, model_names[0]) == 0); return failing("wake-iface") ? NULL : &wake_iface; }
const esp_vadn_iface_t *esp_vadn_handle_from_name(const char *name) { assert(strcmp(name, model_names[1]) == 0); return failing("vad-iface") ? NULL : &vad_iface; }

int main(void)
{
    pet_ai_sr_t sr = {0};
    pet_ai_runtime_frontend_t frontend = pet_ai_sr_frontend(&sr);
    assert(sr.state == NULL && allocations == 0 && loaded == 0);
    assert(frontend.open(frontend.context) == PET_AI_PORT_OK && mappings == 0);
    int16_t pcm[320];
    for (unsigned i = 0; i < 320; ++i) pcm[i] = (int16_t)(i + 1);
    bool detected = true;
    assert(frontend.detect_wake(frontend.context, pcm, 320, 20, &detected) == PET_AI_PORT_OK && !detected);
    for (unsigned i = 0; i < 320; ++i) pcm[i] = (int16_t)(i + 321);
    assert(frontend.detect_wake(frontend.context, pcm, 320, 40, &detected) == PET_AI_PORT_OK && detected);
    assert(wake_detections == 1);
    pet_ai_activity_t activity = PET_AI_ACTIVITY_ENERGY;
    assert(frontend.classify_vad(frontend.context, pcm, 320, 60, &activity) == PET_AI_PORT_OK);
    assert(activity == PET_AI_ACTIVITY_SILENCE); /* 模型未凑满，不回退到能量伪造语音。 */
    assert(frontend.classify_vad(frontend.context, pcm, 320, 80, &activity) == PET_AI_PORT_OK);
    assert(activity == PET_AI_ACTIVITY_SPEECH);
    assert(frontend.reset(frontend.context) == PET_AI_PORT_OK);
    assert(frontend.classify_vad(frontend.context, pcm, 320, 100, &activity) == PET_AI_PORT_OK);
    assert(activity == PET_AI_ACTIVITY_SILENCE);
    assert(frontend.classify_vad(frontend.context, pcm, 320, 99, &activity) == PET_AI_PORT_INVALID);
    assert(frontend.classify_vad(frontend.context, pcm, 319, 120, &activity) == PET_AI_PORT_INVALID);
    assert(frontend.classify_vad(frontend.context, NULL, 320, 120, &activity) == PET_AI_PORT_INVALID);
    assert(frontend.classify_vad(frontend.context, pcm, 320, 120, NULL) == PET_AI_PORT_INVALID);
    /* gap 清掉上一个 20 ms 残帧及标签，不能和新的音频拼成模型帧。 */
    assert(frontend.classify_vad(frontend.context, pcm, 320, 1000, &activity) == PET_AI_PORT_OK);
    assert(activity == PET_AI_ACTIVITY_SILENCE);
    assert(frontend.close(frontend.context) == PET_AI_PORT_OK);
    assert(frontend.close(frontend.context) == PET_AI_PORT_OK);
    assert(sr.state == NULL && allocations == 0 && loaded == 0 && live == 0 && mappings == 0);
    const char *failures[] = {"partition", "map", "crypto-init", "crypto-hash", "digest", "digest-size",
        "alloc", "load", "wake-iface", "vad-iface", "wake-create", "vad-create", "rate", "channels", "chunk"};
    for (unsigned i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        failure = failures[i];
        assert(frontend.open(frontend.context) == PET_AI_PORT_FAILED);
        assert(sr.state == NULL && allocations == 0 && loaded == 0 && live == 0 && mappings == 0);
    }
    failure = "";
    model_names[1] = "unsupported";
    assert(frontend.open(frontend.context) == PET_AI_PORT_FAILED && sr.state == NULL && live == 0 && loaded == 0);
    model_names[1] = "vadnet1_medium";
    partition.size = 0x200000;
    assert(frontend.open(frontend.context) == PET_AI_PORT_FAILED && sr.state == NULL);
    partition.size = 0x100000;
    assert(frontend.open(frontend.context) == PET_AI_PORT_OK);
    assert(frontend.open(frontend.context) == PET_AI_PORT_INVALID);
    assert(frontend.close(frontend.context) == PET_AI_PORT_OK && allocations == 0 && loaded == 0 && live == 0);
    puts("ESP-SR frontend: bounded PCM stitching and owner cleanup passed");
}
