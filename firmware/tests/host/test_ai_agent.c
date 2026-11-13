#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "freertos/task.h"
#include "freertos/queue.h"
#include "svc_ai_agent/api.h"

/* 使用真实主机线程，每次运行到 vTaskDelay；调度边界暂停后再检查公开快照。 */
static pthread_mutex_t scheduler = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static pthread_t worker_id;
static unsigned cycles;
static TaskFunction_t worker;
static int task_token;
static bool notified, task_live, at_gate, permit;
static _Thread_local bool worker_thread;
static uint64_t clock_ms;
static unsigned inits, deinits, starts, stops, forwarded, begun;
static unsigned playback_starts, playback_stops, drains, written_samples;
static int16_t first_output;
static const char *scenario;
static bool failure_armed = true;
static bool capturing, playing, wake_detected = true, vad_speech = true;
static unsigned contexts, queues, queue_creates, reads;
static bool failing(const char *name) { return failure_armed && strcmp(scenario, name) == 0; }
static bool in_worker(void);
static unsigned frontend_opens, frontend_closes, frontend_resets;
static bool frontend_failure_armed;
static pet_ai_port_result_t frontend_open(void *context)
{
    (void)context;
    assert(in_worker());
    ++frontend_opens;
    return failing("frontend-open-fail") ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t frontend_reset(void *context)
{
    (void)context;
    assert(in_worker() && frontend_opens == 1 && frontend_closes == 0);
    ++frontend_resets;
    return failing("frontend-reset-fail") ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t frontend_close(void *context)
{
    (void)context;
    assert(in_worker() && frontend_opens == 1 && frontend_closes == 0);
    ++frontend_closes;
    return PET_AI_PORT_OK;
}
static void wait_change(void)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5;
    assert(pthread_cond_timedwait(&changed, &scheduler, &deadline) == 0);
}

static void run_task(void)
{
    pthread_mutex_lock(&scheduler);
    const unsigned previous = cycles;
    permit = true;
    pthread_cond_broadcast(&changed);
    while (cycles == previous && task_live) wait_change();
    pthread_mutex_unlock(&scheduler);
}
static bool in_worker(void) { return worker_thread; }
static void *task_entry(void *context) { worker_thread = true; worker(context); return NULL; }

int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
void *heap_caps_malloc(size_t size, unsigned caps)
{
    (void)caps;
    if (failing("alloc-fail")) return NULL;
    ++contexts;
    return malloc(size);
}
void heap_caps_free(void *ptr) { assert(contexts > 0); --contexts; free(ptr); }

typedef struct {
    size_t size;
    unsigned length, count, head;
    unsigned char *data;
    pthread_mutex_t lock;
} test_queue_t;
QueueHandle_t xQueueCreate(unsigned length, unsigned item_size)
{
    ++queue_creates;
    if (failing("control-queue-fail") || (failing("playback-queue-fail") && queue_creates == 2))
        return NULL;
    test_queue_t *queue = calloc(1, sizeof(*queue));
    assert(queue);
    queue->data = calloc(length, item_size);
    assert(queue->data);
    queue->length = length;
    queue->size = item_size;
    assert(pthread_mutex_init(&queue->lock, NULL) == 0);
    ++queues;
    return queue;
}
void vQueueDelete(QueueHandle_t handle)
{
    test_queue_t *queue = handle;
    assert(queues > 0);
    --queues;
    pthread_mutex_destroy(&queue->lock);
    free(queue->data);
    free(queue);
}
int xQueueSend(QueueHandle_t handle, const void *value, TickType_t timeout)
{
    test_queue_t *queue = handle;
    assert(timeout == 0);
    pthread_mutex_lock(&queue->lock);
    if (queue->count == queue->length) { pthread_mutex_unlock(&queue->lock); return 0; }
    memcpy(queue->data + ((queue->head + queue->count++) % queue->length) * queue->size,
           value, queue->size);
    pthread_mutex_unlock(&queue->lock);
    return pdTRUE;
}
int xQueueReceive(QueueHandle_t handle, void *value, TickType_t timeout)
{
    test_queue_t *queue = handle;
    assert(in_worker() && timeout == 0);
    pthread_mutex_lock(&queue->lock);
    if (queue->count == 0) { pthread_mutex_unlock(&queue->lock); return 0; }
    memcpy(value, queue->data + queue->head * queue->size, queue->size);
    queue->head = (queue->head + 1) % queue->length;
    --queue->count;
    pthread_mutex_unlock(&queue->lock);
    return pdTRUE;
}
int xQueueReset(QueueHandle_t handle)
{
    test_queue_t *queue = handle;
    assert(in_worker());
    pthread_mutex_lock(&queue->lock);
    queue->count = queue->head = 0;
    pthread_mutex_unlock(&queue->lock);
    return pdPASS;
}
int xTaskCreate(TaskFunction_t function, const char *name, uint32_t stack, void *context,
                unsigned priority, TaskHandle_t *task)
{
    assert(name && stack >= 4096 && priority > 0 && task);
    if (failing("task-fail")) return 0;
    worker = function;
    task_live = true;
    *task = &task_token;
    assert(pthread_create(&worker_id, NULL, task_entry, context) == 0);
    pthread_mutex_lock(&scheduler);
    while (!at_gate) wait_change();
    pthread_mutex_unlock(&scheduler);
    assert(inits == 0);
    return pdPASS;
}
uint32_t ulTaskNotifyTake(int clear, TickType_t timeout)
{
    assert(in_worker() && clear == pdTRUE && timeout == portMAX_DELAY);
    pthread_mutex_lock(&scheduler);
    at_gate = true;
    pthread_cond_broadcast(&changed);
    while (!notified || !permit) wait_change();
    permit = false;
    pthread_mutex_unlock(&scheduler);
    return 1;
}
int xTaskNotifyGive(TaskHandle_t task)
{
    assert(task == &task_token && task_live);
    pthread_mutex_lock(&scheduler);
    notified = true;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&scheduler);
    return pdPASS;
}
void vTaskDelay(TickType_t ticks)
{
    assert(in_worker() && ticks > 0);
    pthread_mutex_lock(&scheduler);
    clock_ms += ticks;
    ++cycles;
    pthread_cond_broadcast(&changed);
    while (!permit) wait_change();
    permit = false;
    pthread_mutex_unlock(&scheduler);
}
void taskYield(void) { vTaskDelay(1); }
void vTaskDelete(TaskHandle_t task)
{
    assert(in_worker() && task == NULL);
    pthread_mutex_lock(&scheduler);
    task_live = false;
    ++cycles;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&scheduler);
    pthread_exit(NULL);
}

static esp_err_t audio_init(void)
{
    assert(in_worker());
    ++inits;
    return failing("init-fail") ? ESP_FAIL : ESP_OK;
}
static esp_err_t audio_deinit(void)
{
    assert(in_worker());
    ++deinits;
    if (failing("deinit-fail")) return ESP_FAIL;
    capturing = playing = false;
    return ESP_OK;
}
static esp_err_t capture_start(void)
{
    assert(in_worker() && !capturing && !playing);
    ++starts;
    if (failing("capture-fail")) return ESP_FAIL;
    capturing = true;
    return ESP_OK;
}
static esp_err_t capture_stop(void)
{
    assert(in_worker());
    ++stops;
    if (failing("stop-fail")) return ESP_FAIL;
    capturing = false;
    return ESP_OK;
}
static esp_err_t capture_read(int16_t *pcm, size_t capacity, size_t *samples, uint32_t timeout)
{
    assert(in_worker() && capturing && !playing && timeout > 0 && capacity <= PET_AUDIO_CAPTURE_SAMPLES);
    ++reads;
    if (failing("privacy-read") && reads == 1)
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.muted = true}) == ESP_OK);
    if (failing("read-fail") || failing("deinit-fail")) { *samples = 0; return ESP_FAIL; }
    if (failing("read-overrun")) { *samples = capacity + 1; return ESP_OK; }
    if (failing("read-stall")) { *samples = 0; clock_ms += timeout; return ESP_ERR_TIMEOUT; }
    if (failing("partial-read") && capacity > 160) capacity = 160;
    for (size_t i = 0; i < capacity; ++i) pcm[i] = 2000;
    *samples = capacity;
    if (!failing("burst-read")) clock_ms += capacity * 1000 / PET_AUDIO_CAPTURE_RATE;
    return failing("partial-read") ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t playback_start(uint32_t rate)
{
    assert(in_worker() && !capturing && !playing && rate == 24000);
    playing = true;
    ++playback_starts;
    return ESP_OK;
}
static esp_err_t playback_stop(void)
{
    assert(in_worker());
    ++playback_stops;
    if (failing("privacy-stop-fail")) return ESP_FAIL;
    playing = false;
    return ESP_OK;
}
static esp_err_t playback_write(const int16_t *pcm, size_t samples, size_t *written, uint32_t timeout)
{
    assert(in_worker() && playing && !capturing && pcm && timeout > 0);
    if (failing("write-fail")) { *written = 0; return ESP_FAIL; }
    if (failing("write-stall")) { *written = 0; clock_ms += timeout; return ESP_ERR_TIMEOUT; }
    if (failing("partial-write") && samples > 240) samples = 240;
    if (written_samples == 0) first_output = pcm[0];
    written_samples += (unsigned)samples;
    *written = samples;
    return failing("partial-write") ? ESP_ERR_TIMEOUT : ESP_OK;
}
static esp_err_t playback_drain(uint32_t timeout)
{
    assert(in_worker() && playing && !capturing && timeout > 0);
    ++drains;
    return failing("drain-fail") ? ESP_ERR_TIMEOUT : ESP_OK;
}
static const pet_audio_device_t audio = {
    .id = "host-i2s", .init = audio_init, .deinit = audio_deinit,
    .capture_start = capture_start, .capture_read = capture_read, .capture_stop = capture_stop,
    .playback_start = playback_start, .playback_write = playback_write,
    .playback_stop = playback_stop, .playback_drain = playback_drain,
};
static pet_ai_port_result_t transport_control(void *context, uint32_t turn)
{
    (void)context;
    assert(in_worker() && turn > 0);
    return PET_AI_PORT_OK;
}
static pet_ai_port_result_t transport_begin(void *context, uint32_t turn)
{
    ++begun;
    return transport_control(context, turn);
}
static pet_ai_port_result_t transport_forward(void *context, uint32_t turn,
                                               const int16_t *pcm, size_t samples)
{
    (void)context;
    assert(in_worker() && turn > 0 && pcm && samples == 320 && pcm[0] == 2000);
    ++forwarded;
    return failing("transport-fail") ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t transport_cancel(void *context, uint32_t turn)
{
    (void)transport_control(context, turn);
    return failing("cancel-fail") ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t wake(void *context, const int16_t *pcm, size_t samples,
                                 uint64_t now, bool *detected)
{
    (void)context; (void)now;
    assert(in_worker() && pcm && samples == 320);
    *detected = wake_detected;
    if (frontend_failure_armed && failing("frontend-wake-fail")) return PET_AI_PORT_FAILED;
    if (failing("frontend-policy-wake"))
        assert(pet_ai_agent_set_dnd(true) == ESP_OK); /* 模拟推理期间到达的并发策略。 */
    if (failing("frontend-cancel-wake"))
        assert(pet_ai_agent_post(PET_AI_CANCEL, 0) == ESP_OK);
    wake_detected = false;
    return PET_AI_PORT_OK;
}
static pet_ai_port_result_t vad(void *context, const int16_t *pcm, size_t samples,
                                uint64_t now, pet_ai_activity_t *activity)
{
    (void)context; (void)now;
    assert(in_worker() && pcm && samples == 320);
    *activity = vad_speech ? PET_AI_ACTIVITY_SPEECH : PET_AI_ACTIVITY_SILENCE;
    if (frontend_failure_armed && failing("frontend-vad-fail")) return PET_AI_PORT_FAILED;
    if (frontend_failure_armed && failing("frontend-vad-invalid")) *activity = (pet_ai_activity_t)99;
    return PET_AI_PORT_OK;
}
static pet_ai_agent_snapshot_t status(void)
{
    pet_ai_agent_snapshot_t snapshot;
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK);
    return snapshot;
}
static void expect_fault(esp_err_t cleanup)
{
    const pet_ai_agent_snapshot_t snapshot = status();
    assert(snapshot.faulted && !snapshot.ready && snapshot.last_error != ESP_OK);
    assert(snapshot.cleanup_error == cleanup && deinits == 1 && !task_live);
    assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_ERR_INVALID_STATE);
    assert(pet_ai_agent_play(1, NULL, 0, true) == ESP_ERR_INVALID_STATE);
    assert(pthread_join(worker_id, NULL) == 0);
}
static void *producer(void *context)
{
    unsigned *accepted = context;
    for (unsigned i = 0; i < 16; ++i) {
        const esp_err_t result = pet_ai_agent_post(PET_AI_TALK_PRESS, 0);
        assert(result == ESP_OK || result == ESP_ERR_TIMEOUT);
        if (result == ESP_OK) ++*accepted;
    }
    return NULL;
}
static void *privacy_producer(void *context)
{
    (void)context;
    for (unsigned i = 0; i < 500; ++i) {
        const pet_ai_policy_t policy = {.muted = i % 2 != 0, .dnd = i % 3 != 0};
        assert(pet_ai_agent_set_policy(&policy) == ESP_OK);
        const esp_err_t result = pet_ai_agent_ptt(true);
        assert(result == ESP_OK || result == ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_ptt(false) == ESP_OK);
        assert(pet_ai_agent_post(PET_AI_CANCEL, 0) == ESP_OK);
    }
    return NULL;
}
int main(int argc, char **argv)
{
    assert(argc <= 2);
    scenario = argc == 2 ? argv[1] : "success";
    pet_ai_agent_config_t config = {
        .audio = &audio, .volume_pct = 25,
        .transport = {.begin_turn = transport_begin, .forward_pcm = transport_forward,
                      .end_input = transport_control, .cancel_turn = transport_cancel},
    };
    if(failing("notification-only")) {
        config.transport=(pet_ai_transport_t){0};config.notifications_only=true;
    }
    if (failing("wake")) config.frontend = (pet_ai_runtime_frontend_t){.detect_wake = wake, .classify_vad = vad};
    if (failing("privacy-wake")) {
        config.frontend.detect_wake = wake;
        config.initial_policy.dnd = true;
    }
    if (strncmp(scenario, "frontend-", 9) == 0) {
        config.frontend = (pet_ai_runtime_frontend_t){
            .open = frontend_open, .reset = frontend_reset, .close = frontend_close,
            .detect_wake = wake, .classify_vad = vad,
        };
        wake_detected = failing("frontend-policy-wake") || failing("frontend-cancel-wake");
    }
    pet_ai_agent_snapshot_t snapshot;
    assert(pet_ai_agent_start(NULL) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_snapshot(NULL) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_post(PET_AI_TICK, 0) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_post(PET_AI_PLAYBACK_DRAINED, 1) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_post(PET_AI_TALK_RELEASE, 0) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 1) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_play(0, NULL, 0, true) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_play(1, NULL, 0, false) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_ERR_INVALID_STATE);
    assert(pet_ai_agent_set_policy(NULL) == ESP_ERR_INVALID_ARG);
    assert(pet_ai_agent_ptt(true) == ESP_ERR_INVALID_STATE);
    assert(pet_ai_agent_play(1, NULL, 0, true) == ESP_ERR_INVALID_STATE);
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_ERR_INVALID_STATE);
    if (failing("alloc-fail") || failing("control-queue-fail") || failing("playback-queue-fail") || failing("task-fail")) {
        assert(pet_ai_agent_start(&config) == ESP_ERR_NO_MEM);
        assert(contexts == 0 && queues == 0 && inits == 0);
        failure_armed = false; /* 同进程重试，不保留悬空队列或半发布状态。 */
    }
    assert(pet_ai_agent_start(&config) == ESP_OK);
    assert(inits == 0 && starts == 0);
    run_task();
    if (failing("init-fail")) { expect_fault(ESP_OK); puts("AI agent: init failure cleaned by owner passed"); return 0; }
    if (failing("frontend-open-fail")) {
        expect_fault(ESP_OK);
        assert(frontend_opens == 1 && frontend_closes == 0 && starts == 0);
        puts("AI agent: frontend init failure prevents recording passed");
        return 0;
    }
    if (failing("frontend-policy-wake")) {
        assert(status().view.state == PET_AI_MUTED && !capturing && begun == 0 && forwarded == 0);
        puts("AI agent: privacy arriving during wake inference prevents cloud turn passed");
        return 0;
    }
    if (failing("frontend-cancel-wake")) {
        assert(status().view.state == PET_AI_IDLE && begun == 0 && forwarded == 0);
        puts("AI agent: cancel arriving during wake inference prevents cloud turn passed");
        return 0;
    }
    if (failing("frontend-wake-fail") || failing("frontend-vad-fail") ||
        failing("frontend-vad-invalid")) {
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        run_task();
        assert(status().view.turn_id == 1 && status().view.state == PET_AI_LISTENING);
        const unsigned accepted_frames = forwarded;
        if (failing("frontend-wake-fail"))
            assert(pet_ai_agent_post(PET_AI_CANCEL, 0) == ESP_OK);
        frontend_failure_armed = true;
        run_task();
        expect_fault(ESP_OK);
        assert(frontend_closes == 1 && !capturing && forwarded == accepted_frames);
        puts("AI agent: model detection failure closes frontend and prevents further capture passed");
        return 0;
    }
    if (strncmp(scenario, "frontend-", 9) == 0) {
        assert(frontend_opens == 1 && frontend_closes == 0 && capturing && forwarded == 0);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        run_task();
        if (failing("frontend-reset-fail")) {
            expect_fault(ESP_FAIL);
            assert(frontend_closes == 1 && !capturing);
        } else {
            assert(status().view.state == PET_AI_MUTED && !capturing && frontend_resets > 0);
            scenario = "read-fail";
            assert(pet_ai_agent_post(PET_AI_UNMUTE, 0) == ESP_OK);
            run_task();
            expect_fault(ESP_OK);
            assert(frontend_closes == 1);
        }
        puts("AI agent: frontend owner lifecycle and privacy reset passed");
        return 0;
    }
    if (failing("privacy-wake")) {
        assert(status().view.state == PET_AI_MUTED && starts == 0 && reads == 0);
        wake_detected = false;
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){0}) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && capturing && starts == 1 && forwarded == 0);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.interrupted = true}) == ESP_OK);
        const unsigned before = reads;
        run_task();
        assert(!capturing && reads == before && status().view.turn_id == 0);
        assert(pet_ai_agent_post(PET_AI_WAKE_DETECTED, 0) == ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){0}) == ESP_OK);
        run_task();
        assert(capturing && starts == 2 && status().view.state == PET_AI_IDLE);
        puts("AI agent: initial DND and critical policy gate idle wake capture passed");
        return 0;
    }
    if (failing("wake")) {
        assert(status().view.state == PET_AI_LISTENING && capturing && starts == 1);
        for (unsigned i = 0; i < 15; ++i) run_task();
        vad_speech = false;
        for (unsigned i = 0; i < 15; ++i) run_task();
        assert(status().view.state == PET_AI_THINKING && forwarded == 30 && !capturing);
        assert(pet_ai_agent_post(PET_AI_CANCEL, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && capturing && starts == 2);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        const unsigned before = reads;
        run_task();
        assert(status().view.state == PET_AI_MUTED && !capturing && reads == before);
        assert(pet_ai_agent_post(PET_AI_UNMUTE, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && capturing && starts == 3);
        puts("AI agent: wake capture, final PCM, half duplex, mute and unmute passed");
        return 0;
    }
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.ready && !snapshot.faulted);
    assert(inits == 1 && starts == 0); /* 未配置唤醒器，空闲不开麦。 */
    if (failing("notification")||failing("notification-only")||failing("notification-critical")) {
        if(failing("notification-only")) {
            assert(!status().voice_enabled);
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS,0)==ESP_ERR_INVALID_STATE);
            assert(pet_ai_agent_ptt(true)==ESP_ERR_INVALID_STATE);
        }
        bool critical=failing("notification-critical");
        if(critical){assert(pet_ai_agent_set_interrupted(true)==ESP_OK);run_task();}
        assert(pet_ai_agent_notify(PET_MESSAGE_SOUND_CHIME,critical)==ESP_OK);
        run_task();assert(playing && playback_starts==1 && starts==0);
        if(critical){run_task();assert(playing&&playback_starts==1);}
        assert(pet_ai_agent_set_dnd(true)==ESP_OK);run_task();
        assert(!playing&&!capturing&&status().view.state==PET_AI_MUTED);
        assert(pet_ai_agent_notify(PET_MESSAGE_SOUND_ALERT,true)==ESP_ERR_INVALID_STATE);
        puts("AI audio: notification has one HAL owner and DND preempts without opening mic passed");
        return 0;
    }
    assert(pet_ai_agent_start(&config) == ESP_ERR_INVALID_STATE);
    if (failing("privacy-patches")) {
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.muted = true, .dnd = true,
                                                       .interrupted = true}) == ESP_OK);
        assert(pet_ai_agent_set_interrupted(false) == ESP_OK);
        assert(status().policy.dnd && status().policy.muted && !status().policy.interrupted);
        pet_ai_context_t history;
        assert(pet_ai_agent_context(&history)==ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_set_dnd(false) == ESP_OK);
        assert(status().policy.muted && !status().policy.dnd);
        run_task();
        assert(status().view.state == PET_AI_MUTED);
        assert(pet_ai_agent_post(PET_AI_UNMUTE, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && !capturing);
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        run_task();
        uint32_t turn=status().view.turn_id;
        assert(pet_ai_agent_text(turn,PET_AI_TEXT_USER,"private transcript")==ESP_OK);
        assert(pet_ai_agent_forget()==ESP_OK);
        assert(!status().transcript[0]&&!status().reply[0]);
        assert(pet_ai_agent_text(turn,PET_AI_TEXT_ASSISTANT,"stale reply")==ESP_ERR_INVALID_STATE);
        assert(!status().transcript[0]&&!status().reply[0]);
        assert(pet_ai_agent_context(&history)==ESP_ERR_INVALID_STATE);
        run_task();
        assert(status().view.state==PET_AI_IDLE&&!capturing);
        puts("AI agent: independent policy updates preserve concurrent gates passed");
        return 0;
    }
    if (failing("privacy-concurrent")) {
        pthread_t producers[2];
        for (unsigned i = 0; i < 2; ++i)
            assert(pthread_create(&producers[i], NULL, privacy_producer, NULL) == 0);
        for (unsigned i = 0; i < 100; ++i) run_task();
        for (unsigned i = 0; i < 2; ++i) assert(pthread_join(producers[i], NULL) == 0);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.dnd = true}) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !status().policy_pending);
        assert(!capturing && !playing && !status().ptt_requested && !status().faulted);
        puts("AI agent: concurrent privacy/PTT/cancel requests converge in the audio owner passed");
        return 0;
    }
    if (failing("privacy-read")) {
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !capturing && forwarded == 0);
        assert(status().captured_frames == 0 && !status().policy_pending);
        puts("AI agent: privacy arriving inside a hardware read discards in-flight PCM passed");
        return 0;
    }
    if (failing("priority-cancel")) {
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        run_task();
        for (unsigned i = 0; i < 8; ++i) assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        assert(pet_ai_agent_post(PET_AI_CANCEL, 0) == ESP_OK);
        for (unsigned i = 0; i < 10; ++i) run_task();
        assert(status().view.state == PET_AI_IDLE && !capturing && starts == 1);
        assert(pet_ai_agent_ptt(false) == ESP_OK);
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        run_task();
        assert(status().view.turn_id == 2 && capturing);
        puts("AI agent: priority cancel invalidates queued starts and requires a fresh press passed");
        return 0;
    }
    if (failing("privacy")) {
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        for (unsigned i = 0; i < 15; ++i) run_task();
        assert(status().view.state == PET_AI_LISTENING && status().capture_active);
        assert(pet_ai_agent_ptt(false) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_THINKING && !status().capture_active);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.dnd = true}) == ESP_OK);
        assert(status().policy_pending);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !status().policy_pending);
        assert(pet_ai_agent_ptt(true) == ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        assert(pet_ai_agent_post(PET_AI_UNMUTE, 0) == ESP_OK);
        run_task();
        assert(status().policy.dnd && !status().policy.muted && status().view.state == PET_AI_MUTED);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){0}) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && !capturing && !playing);
        assert(pet_ai_agent_ptt(true) == ESP_OK);
        run_task();
        assert(status().view.turn_id == 2 && capturing);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.interrupted = true}) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && !capturing && !playing);
        assert(pet_ai_agent_ptt(true) == ESP_ERR_INVALID_STATE);
        assert(pet_ai_agent_ptt(false) == ESP_OK);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){0}) == ESP_OK);
        run_task();
        assert(status().view.turn_id == 2 && !capturing);
        puts("AI agent: PTT release, DND, independent mute and critical interruption passed");
        return 0;
    }
    if (failing("priority-mute")) {
        for (unsigned i = 0; i < 8; ++i)
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !capturing && starts == 0);
        assert(pet_ai_agent_post(PET_AI_UNMUTE, 0) == ESP_OK);
        for (unsigned i = 0; i < 10; ++i) run_task();
        assert(status().view.state == PET_AI_IDLE && starts == 0 && status().view.turn_id == 0);
        puts("AI agent: mute bypasses full queue and invalidates old starts passed");
        return 0;
    }
    if (failing("queue-full")) {
        unsigned accepted[2] = {0};
        pthread_t producers[2];
        for (unsigned i = 0; i < 2; ++i)
            assert(pthread_create(&producers[i], NULL, producer, &accepted[i]) == 0);
        for (unsigned i = 0; i < 2; ++i) assert(pthread_join(producers[i], NULL) == 0);
        assert(accepted[0] + accepted[1] == 8 && status().rejected_commands == 24);
        int16_t pcm[480] = {1000};
        for (unsigned i = 0; i < 8; ++i) assert(pet_ai_agent_play(1, pcm, 480, false) == ESP_OK);
        assert(pet_ai_agent_play(1, pcm, 480, false) == ESP_ERR_TIMEOUT);
        for (unsigned i = 0; i < 8; ++i) run_task();
        assert(status().captured_frames == 8 && status().view.turn_id == 1 && forwarded == 8);
        assert(status().rejected_commands == 25);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !capturing && !playing);
        puts("AI agent: concurrent bounded queues, explicit rejection, PCM progress and priority mute passed");
        return 0;
    }
    assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK && starts == 0);
    const unsigned frame_cycles = failing("partial-read") ? 30 : failing("burst-read") ? 16 : 15;
    for (unsigned i = 0; i < frame_cycles; ++i) {
        run_task();
        if (!task_live) break;
        if (failing("transport-fail")) {
            assert(status().view.state == PET_AI_ERROR && status().ready && !capturing);
            assert(status().view.reason == PET_AI_REASON_TRANSPORT_ERROR);
            failure_armed = false;
            assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
            run_task();
            assert(status().view.turn_id == 2 && status().view.state == PET_AI_LISTENING && inits == 1);
            puts("AI agent: failed transport cleanup and retry passed");
            return 0;
        }
    }
    if (failing("capture-fail") || failing("read-fail") || failing("read-overrun") || failing("deinit-fail")) {
        expect_fault(failing("deinit-fail") ? ESP_FAIL : ESP_OK);
        assert(status().view.reason == PET_AI_REASON_AUDIO_ERROR);
        puts("AI agent: hardware failure latches service and reports cleanup passed");
        return 0;
    }
    if (failing("read-stall")) {
        expect_fault(ESP_OK);
        assert(status().view.state == PET_AI_ERROR && status().view.reason == PET_AI_REASON_AUDIO_ERROR);
        assert(status().captured_frames == 0);
        puts("AI agent: stalled capture latches audio fault without false speech passed");
        return 0;
    }
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.view.state == PET_AI_LISTENING);
    assert(starts == 1 && forwarded == 15 && snapshot.captured_frames == 15);
    assert(pet_ai_agent_post(PET_AI_TALK_RELEASE, 1) == ESP_OK);
    run_task();
    if (failing("stop-fail")) { expect_fault(ESP_FAIL); puts("AI agent: stop failure prevents reuse passed"); return 0; }
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.view.state == PET_AI_THINKING);
    assert(stops == 1 && deinits == 0);
    if (failing("cancel-fail")) {
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        run_task();
        expect_fault(ESP_FAIL);
        assert(starts == 1 && status().view.turn_id == 2);
        puts("AI agent: old turn cancellation failure prevents new capture passed");
        return 0;
    }
    assert(pet_ai_agent_post(PET_AI_RESPONSE_BEGIN, 99) == ESP_OK);
    run_task();
    assert(status().view.state == PET_AI_THINKING && status().stale_items == 1 && playback_starts == 0);
    int16_t pcm[PET_AUDIO_PLAYBACK_SAMPLES];
    for (unsigned i = 0; i < PET_AUDIO_PLAYBACK_SAMPLES; ++i) pcm[i] = 2000;
    assert(pet_ai_agent_post(PET_AI_RESPONSE_BEGIN, 1) == ESP_OK);
    if (failing("empty-playback") || failing("playback-stall")) {
        if (failing("empty-playback")) assert(pet_ai_agent_play(1, NULL, 0, true) == ESP_OK);
        run_task();
        for (unsigned i = 0; status().view.state == PET_AI_SPEAKING && i < 30; ++i) run_task();
        assert(status().view.state == PET_AI_ERROR && status().view.reason == PET_AI_REASON_TRANSPORT_ERROR);
        assert(status().ready && !playing && drains == 0 && written_samples == 0);
        puts("AI agent: empty or stalled response never reports playback complete passed");
        return 0;
    }
    if (failing("prebuffer")) {
        assert(pet_ai_agent_play(1,pcm,480,false)==ESP_OK);
        run_task();
        assert(status().view.state==PET_AI_SPEAKING && playback_starts==0 && written_samples==0);
        for (unsigned i=0;i<4;++i) assert(pet_ai_agent_play(1,pcm,480,false)==ESP_OK);
        run_task();
        assert(playback_starts==1 && written_samples==480);
        for (unsigned i=0;i<4;++i) run_task();
        run_task();
        run_task();
        assert(status().playback_underruns>0);
        assert(pet_ai_agent_play(1,pcm,480,false)==ESP_OK);
        assert(pet_ai_agent_play(1,NULL,0,true)==ESP_OK);
        run_task(); run_task();
        assert(status().view.state==PET_AI_IDLE && drains==1);
        puts("AI agent: 100ms prebuffer, bounded underrun and recovery passed");
        return 0;
    }
    assert(pet_ai_agent_play(99, pcm, PET_AUDIO_PLAYBACK_SAMPLES, false) == ESP_OK);
    assert(pet_ai_agent_play(1, pcm, PET_AUDIO_PLAYBACK_SAMPLES, false) == ESP_OK);
    memset(pcm, 0, sizeof(pcm)); /* 生产者可立即复用缓冲区。 */
    assert(pet_ai_agent_play(1, NULL, 0, true) == ESP_OK);
    run_task();
    if (failing("write-fail")) { expect_fault(ESP_OK); puts("AI agent: write failure passed"); return 0; }
    if (failing("write-stall")) {
        for (unsigned i = 0; task_live && i < 10; ++i) run_task();
        expect_fault(ESP_OK);
        assert(drains == 0);
        puts("AI agent: write stall never reports drained passed");
        return 0;
    }
    if (failing("partial-write")) {
        assert(written_samples == 240 && drains == 0);
        run_task();
    }
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.view.state == PET_AI_SPEAKING);
    assert(playback_starts == 1 && written_samples == 480 && first_output == 500 && drains == 0);
    assert(status().stale_items == 2);
    if (failing("privacy-playback") || failing("privacy-stop-fail")) {
        for (unsigned i = 0; i < 8; ++i) assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){.dnd = true}) == ESP_OK);
        run_task();
        if (failing("privacy-stop-fail")) {
            expect_fault(ESP_FAIL);
            assert(status().view.state == PET_AI_MUTED);
            puts("AI agent: failed privacy cleanup reports fault instead of physical mute passed");
            return 0;
        }
        for (unsigned i = 0; i < 10; ++i) run_task();
        assert(status().view.state == PET_AI_MUTED && !playing && !capturing);
        assert(playback_stops == 1 && drains == 0 && written_samples == 480 && starts == 1);
        assert(pet_ai_agent_set_policy(&(pet_ai_policy_t){0}) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_IDLE && !playing && !capturing && starts == 1);
        puts("AI agent: DND stops playback, clears queued audio and does not resume old work passed");
        return 0;
    }
    if (failing("barge-in")) {
        assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
        run_task();
        assert(status().view.turn_id == 2 && status().view.state == PET_AI_LISTENING);
        assert(!playing && capturing && drains == 0 && playback_stops == 1 && starts == 2);
        assert(pet_ai_agent_post(PET_AI_RESPONSE_BEGIN, 1) == ESP_OK);
        assert(pet_ai_agent_play(1, pcm, 480, false) == ESP_OK);
        run_task();
        assert(status().stale_items == 3 && written_samples == 480);
        assert(pet_ai_agent_post(PET_AI_MUTE, 0) == ESP_OK);
        run_task();
        assert(status().view.state == PET_AI_MUTED && !capturing && !playing && drains == 0);
        puts("AI agent: barge-in discards old playback, rejects old response and mutes passed");
        return 0;
    }
    run_task();
    if (failing("drain-fail")) {
        expect_fault(ESP_OK);
        assert(status().view.reason == PET_AI_REASON_AUDIO_ERROR);
        puts("AI agent: drain timeout never reports playback complete passed");
        return 0;
    }
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.view.state == PET_AI_IDLE);
    assert(drains == 1 && playback_stops == 1 && snapshot.submitted_samples == 480);
    assert(pet_ai_agent_post(PET_AI_TALK_PRESS, 0) == ESP_OK);
    for (unsigned i = 0; i < frame_cycles; ++i) run_task();
    assert(pet_ai_agent_post(PET_AI_TALK_RELEASE, 2) == ESP_OK);
    run_task();
    clock_ms += PET_AI_RESPONSE_TIMEOUT_MS;
    run_task();
    assert(pet_ai_agent_snapshot(&snapshot) == ESP_OK && snapshot.view.state == PET_AI_ERROR);
    assert(snapshot.view.reason == PET_AI_REASON_RESPONSE_TIMEOUT);
    puts("AI agent: task ownership, queued controls, PCM capture/playback, drain and timeout passed");
    return 0;
}
