#include <assert.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "svc_audio/api.h"
#include "freertos/task.h"

static bool permit = true, cancel_mid, cancel_init, fail_alloc, fail_task, ready, capturing, playing;
static unsigned allocations, initialized, starts, received, submitted;
static int64_t now_us;
static TaskFunction_t scheduled;
static void *scheduled_context;

void host_audio_diagnostic_log(const char *tag, const char *format, ...)
{
    char line[512]; va_list args;
    assert(strcmp(tag, "audio_check") == 0);
    va_start(args, format); assert(vsnprintf(line, sizeof(line), format, args) > 0); va_end(args);
}
const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "host-diagnostic-error"; }
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    (void)caps; if (fail_alloc) return NULL;
    void *value = calloc(count, size); assert(value != NULL); ++allocations; return value;
}
void heap_caps_free(void *value) { assert(value != NULL && allocations > 0); --allocations; free(value); }
int xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *context,
                unsigned priority, TaskHandle_t *task)
{
    assert(strcmp(name, "pet_audio_check") == 0 && stack == 6144 && priority == 3 && task == NULL);
    if (fail_task) return 0;
    assert(scheduled == NULL); scheduled = function; scheduled_context = context; return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(task == NULL); }
int64_t esp_timer_get_time(void) { return now_us; }
static bool allowed(void *ctx)
{
    assert(ctx == &permit);
    return permit && !(cancel_mid && received > 0);
}
static esp_err_t init(void) { assert(!ready && allowed(&permit)); ready = true; ++initialized; if (cancel_init) permit = false; return ESP_OK; }
static esp_err_t deinit(void) { ready = capturing = playing = false; return ESP_OK; }
static esp_err_t capture_start(void) { assert(ready && !playing); ++starts; capturing = true; return ESP_OK; }
static esp_err_t duplex_start(uint32_t rate)
{
    assert(ready && rate == 24000); ++starts; capturing = playing = true; return ESP_OK;
}
static esp_err_t read_pcm(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout)
{
    assert(capturing && capacity == 320 && timeout == 100);
    memset(pcm, 0, capacity * sizeof(*pcm)); *samples = capacity; received += capacity; now_us += 20000; return ESP_OK;
}
static esp_err_t capture_stop(void) { capturing = false; return ESP_OK; }
static esp_err_t playback_start(uint32_t rate) { assert(!capturing && rate == 16000); playing = true; return ESP_OK; }
static esp_err_t write_pcm(const int16_t *pcm, size_t count, size_t *written, uint32_t timeout)
{
    assert(playing && pcm && timeout == 100); *written = count; submitted += count; now_us += 1000; return ESP_OK;
}
static esp_err_t drain(uint32_t timeout) { assert(playing && !capturing && timeout == 250); return ESP_OK; }
static esp_err_t playback_stop(void) { playing = false; return ESP_OK; }
static const pet_audio_device_t device = {.id = "host-policy-boundary", .init = init, .deinit = deinit,
    .capture_start = capture_start, .capture_read = read_pcm, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = write_pcm, .playback_drain = drain,
    .playback_stop = playback_stop, .duplex_start = duplex_start};
const pet_audio_device_t *pet_bsp_audio(void) { return &device; }

int main(int argc, char **argv)
{
    assert(argc == 1 || argc == 2);
    const char *scenario = argc == 2 ? argv[1] : "allowed";
    assert(pet_audio_diagnostic_start(NULL, NULL) == ESP_ERR_INVALID_ARG);
    permit = false;
    assert(pet_audio_diagnostic_start(allowed, &permit) == ESP_ERR_INVALID_STATE);
    assert(allocations == 0 && initialized == 0 && scheduled == NULL);
    permit = true;
    if (strcmp(scenario, "allocation") == 0 && !CONFIG_PET_AUDIO_DIAGNOSTIC_DUPLEX) {
        fail_alloc = true;
        assert(pet_audio_diagnostic_start(allowed, &permit) == ESP_ERR_NO_MEM);
        assert(allocations == 0 && scheduled == NULL); fail_alloc = false;
    }
    if (strcmp(scenario, "task") == 0) {
        fail_task = true;
        assert(pet_audio_diagnostic_start(allowed, &permit) == ESP_ERR_NO_MEM);
        assert(allocations == 0 && initialized == 0); fail_task = false;
    }
    assert(pet_audio_diagnostic_start(allowed, &permit) == ESP_OK);
    if (strcmp(scenario, "before") == 0) permit = false;
    cancel_mid = strcmp(scenario, "mid") == 0;
    cancel_init = strcmp(scenario, "init") == 0;
    assert(scheduled != NULL); scheduled(scheduled_context);
    assert(allocations == 0 && !ready && !capturing && !playing);
    if (cancel_init) assert(initialized == 1 && starts == 0 && received == 0 && submitted == 0);
    else if (!permit) assert(initialized == 0 && starts == 0 && received == 0 && submitted == 0);
    else if (cancel_mid) assert(initialized == 1 && received == 320 && submitted == (CONFIG_PET_AUDIO_DIAGNOSTIC_DUPLEX ? 480 : 0));
    else assert(initialized == 1 && received == 16000 && submitted == (CONFIG_PET_AUDIO_DIAGNOSTIC_DUPLEX ? 24000 : 16000));
    permit = true;
    assert(pet_audio_diagnostic_start(allowed, &permit) == ESP_ERR_INVALID_STATE);
    puts("boot audio diagnostic: policy before task, frame cancellation, allocation/task failures and one-shot ownership passed");
}
