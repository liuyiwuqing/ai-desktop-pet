#include "svc_ai_agent/api.h"

#include <string.h>
#include <math.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pet/hal/audio_pcm.h"
#include "pet/hal/audio_buffer.h"

#define CONTROL_DEPTH 8u
#define PLAYBACK_DEPTH 8u
#define PREBUFFER_SAMPLES 2400u
#define IO_TIMEOUT_MS 20u
#define PLAYBACK_STALL_MS 500u

typedef struct {
    pet_ai_input_kind_t kind;
    uint32_t turn_id;
    uint64_t epoch;
} control_t;

typedef struct {
    uint32_t turn_id;
    size_t samples;
    bool finish, silence;
    int16_t pcm[PET_AUDIO_PLAYBACK_SAMPLES];
} playback_t;

typedef struct {
    pet_ai_agent_config_t config;
    pet_ai_runtime_t runtime;
    pet_ai_context_t context; uint64_t context_updated_ms;
    QueueHandle_t controls, playback;
    pet_ai_agent_snapshot_t status;
    esp_err_t hardware_error;
    bool capturing, frontend_opened;
    int16_t capture_pcm[PET_AUDIO_CAPTURE_SAMPLES];
    size_t capture_fill;
    uint64_t capture_progress_ms;
    uint64_t next_frame_ms;
    playback_t current;
    size_t playback_offset;
    uint64_t playback_progress_ms;
    bool submitted_audio, playing, cancel_pending, response_ended;
    pet_audio_buffer_t prebuffer;
    int16_t prebuffer_pcm[PREBUFFER_SAMPLES + PET_AUDIO_PLAYBACK_SAMPLES];
    int16_t last_output;
    uint64_t next_silence_ms;
    pet_message_sound_t notification;
    bool notification_critical,tone_active;
    pet_message_sound_t tone_sound;
    uint64_t tone_progress_ms;
    uint64_t notification_ms;
    uint32_t tone_offset;
    uint64_t control_epoch, applied_epoch, ptt_revision, seen_ptt_revision;
    uint32_t ptt_turn;
} agent_t;

static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static agent_t *agent;
static bool inhibited(agent_t *owner);
static bool apply_priority(agent_t *owner);

static uint64_t monotonic_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }

static void publish_status(agent_t *owner)
{
    pet_ai_view_t view;
    (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
    uint32_t completed=0;
    portENTER_CRITICAL(&status_lock);
    if (view.state==PET_AI_IDLE && view.reason==PET_AI_REASON_DRAINED && owner->status.view.state!=PET_AI_IDLE) completed=view.turn_id;
    if (view.state==PET_AI_IDLE && view.reason==PET_AI_REASON_DRAINED &&
        owner->status.view.state!=PET_AI_IDLE && (owner->status.transcript[0] || owner->status.reply[0])) {
        (void)pet_ai_context_add(&owner->context,view.turn_id,owner->status.transcript,owner->status.reply);
        owner->context_updated_ms=monotonic_ms();
    }
    if (owner->status.policy.muted || owner->status.policy.dnd || owner->status.policy.interrupted ||
        (owner->context_updated_ms && monotonic_ms()-owner->context_updated_ms>=300000)) {
        pet_ai_context_clear(&owner->context); owner->context_updated_ms=0;
        memset(owner->status.transcript,0,sizeof(owner->status.transcript));
        memset(owner->status.reply,0,sizeof(owner->status.reply));
    }
    if (owner->status.view.turn_id!=view.turn_id || view.state==PET_AI_MUTED ||
        (view.state==PET_AI_IDLE && view.reason!=PET_AI_REASON_DRAINED)) {
        memset(owner->status.transcript,0,sizeof(owner->status.transcript));
        memset(owner->status.reply,0,sizeof(owner->status.reply));
    }
    owner->status.view = view;
    owner->status.capture_active = owner->capturing;
    owner->status.playback_active = owner->playing;
    owner->status.buffered_samples=(uint16_t)owner->prebuffer.count;
    if (!owner->playing) owner->status.playback_level_pct=0;
    owner->status.policy_pending = owner->control_epoch != owner->applied_epoch;
    portEXIT_CRITICAL(&status_lock);
    if (completed && owner->config.on_completed) owner->config.on_completed(completed);
}

static pet_ai_port_result_t audio_result(agent_t *owner, esp_err_t error)
{
    if (error == ESP_OK) return PET_AI_PORT_OK;
    if (owner->hardware_error == ESP_OK) owner->hardware_error = error;
    return PET_AI_PORT_FAILED;
}

static void record_inference(agent_t *owner,uint64_t began)
{
    uint32_t elapsed=(uint32_t)(monotonic_ms()-began);
    portENTER_CRITICAL(&status_lock);++owner->status.inference_count;owner->status.inference_last_ms=elapsed;
    if (elapsed>owner->status.inference_max_ms) owner->status.inference_max_ms=elapsed;
    portEXIT_CRITICAL(&status_lock);
}

static pet_ai_port_result_t frontend_wake(void *context, const int16_t *pcm, size_t samples,
                                          uint64_t now, bool *detected)
{
    agent_t *owner = context;
    uint64_t began=monotonic_ms();
    const pet_ai_port_result_t result = owner->config.frontend.detect_wake(
        owner->config.frontend.context, pcm, samples, now, detected);
    record_inference(owner,began);
    /* 模型属于音频资源；检测失败须锁住拥有者，不能按网络错误重新监听。 */
    return audio_result(owner, result == PET_AI_PORT_OK ? ESP_OK : ESP_FAIL);
}

static pet_ai_port_result_t frontend_vad(void *context, const int16_t *pcm, size_t samples,
                                         uint64_t now, pet_ai_activity_t *activity)
{
    agent_t *owner = context;
    uint64_t began=monotonic_ms();
    const pet_ai_port_result_t result = owner->config.frontend.classify_vad(
        owner->config.frontend.context, pcm, samples, now, activity);
    record_inference(owner,began);
    return audio_result(owner, result == PET_AI_PORT_OK && *activity >= PET_AI_ACTIVITY_ENERGY &&
        *activity <= PET_AI_ACTIVITY_SILENCE ? ESP_OK : ESP_FAIL);
}

static pet_ai_port_result_t frontend_preprocess(void *context,const int16_t *pcm,size_t samples,
    uint64_t now,int16_t *out)
{
    agent_t *owner=context;uint64_t began=monotonic_ms();
    pet_ai_port_result_t result=owner->config.frontend.preprocess(owner->config.frontend.context,pcm,samples,now,out);
    record_inference(owner,began);
    return audio_result(owner,result==PET_AI_PORT_OK?ESP_OK:ESP_FAIL);
}

static pet_ai_port_result_t begin_turn(void *context, uint32_t turn)
{
    agent_t *owner = context;
    /* 推理期间可到达隐私请求；在异步网络入口前再检查，随后由优先邮箱清理状态。 */
    if (inhibited(owner)) return PET_AI_PORT_OK;
    return owner->config.transport.begin_turn ? owner->config.transport.begin_turn(owner->config.transport.context, turn) : PET_AI_PORT_FAILED;
}
static pet_ai_port_result_t forward_pcm(void *context, uint32_t turn,
                                        const int16_t *pcm, size_t samples)
{
    agent_t *owner = context;
    if (inhibited(owner)) return PET_AI_PORT_OK; /* 已读的在途帧不再进入新网络发送。 */
    return owner->config.transport.forward_pcm ? owner->config.transport.forward_pcm(owner->config.transport.context, turn, pcm, samples) : PET_AI_PORT_FAILED;
}
static pet_ai_port_result_t end_input(void *context, uint32_t turn)
{
    agent_t *owner = context;
    return owner->config.transport.end_input ? owner->config.transport.end_input(owner->config.transport.context, turn) : PET_AI_PORT_FAILED;
}
static pet_ai_port_result_t cancel_turn(void *context, uint32_t turn)
{
    agent_t *owner = context;
    return owner->config.transport.cancel_turn ? owner->config.transport.cancel_turn(owner->config.transport.context, turn) : PET_AI_PORT_OK;
}
static pet_ai_port_result_t capture_start(void *context, uint32_t turn)
{
    (void)turn;
    agent_t *owner = context;
    if (inhibited(owner)) return PET_AI_PORT_OK;
    if (owner->tone_active) {
        if (owner->config.audio->playback_stop()!=ESP_OK) return audio_result(owner,ESP_FAIL);
        owner->tone_active=owner->playing=false;
    }
    owner->next_frame_ms = monotonic_ms() + 1;
    if (owner->capturing) return PET_AI_PORT_OK; /* 唤醒检测已持有同一个 RX。 */
    const esp_err_t error = owner->config.audio->capture_start();
    if (error == ESP_OK) {
        owner->capturing = true;
        owner->capture_progress_ms = monotonic_ms();
    }
    return audio_result(owner, error);
}
static pet_ai_port_result_t capture_stop(void *context, uint32_t turn)
{
    (void)turn;
    agent_t *owner = context;
    const esp_err_t error = owner->config.audio->capture_stop();
    if (error == ESP_OK) owner->capturing = false;
    owner->capture_fill = 0;
    /* session 可能仍需转发本帧再 end_input；不能在 STOP_CAPTURE 时擦掉它。 */
    if (owner->config.frontend.reset != NULL &&
        owner->config.frontend.reset(owner->config.frontend.context) != PET_AI_PORT_OK)
        return audio_result(owner, ESP_FAIL);
    return audio_result(owner, error);
}
static pet_ai_port_result_t playback_start(void *context, uint32_t turn)
{
    (void)turn;
    agent_t *owner = context;
    owner->submitted_audio = owner->response_ended = false;
    owner->playback_progress_ms = monotonic_ms();
    owner->last_output=0;
    pet_audio_buffer_clear(&owner->prebuffer);
    /* 会话已进入 SPEAKING；HAL 留到预缓冲就绪后才打开。 */
    return PET_AI_PORT_OK;
}
static pet_ai_port_result_t playback_stop(void *context, uint32_t turn)
{
    (void)turn;
    agent_t *owner = context;
    /* 隐私请求立即停；普通取消最多追加 10 ms 淡出，且只由 HAL 拥有者执行。 */
    if (owner->playing && owner->last_output && !inhibited(owner)) {
        int16_t fade[240];
        for (size_t i=0;i<240;++i) fade[i]=(int16_t)((int32_t)owner->last_output*(239-(int)i)/240);
        size_t written=0;
        (void)owner->config.audio->playback_write(fade,240,&written,IO_TIMEOUT_MS);
    }
    const esp_err_t error = owner->config.audio->playback_stop();
    if (error == ESP_OK) owner->playing = false;
    return audio_result(owner, error);
}
static pet_ai_port_result_t clear_audio(void *context, uint32_t turn)
{
    (void)turn;
    agent_t *owner = context;
    pet_audio_buffer_clear(&owner->prebuffer);
    owner->last_output=0; owner->response_ended=false;
    owner->capture_fill = owner->playback_offset = 0;
    memset(owner->capture_pcm, 0, sizeof(owner->capture_pcm));
    memset(&owner->current, 0, sizeof(owner->current));
    xQueueReset(owner->playback);
    if (owner->config.frontend.reset != NULL)
        return owner->config.frontend.reset(owner->config.frontend.context);
    return PET_AI_PORT_OK;
}

static void fault(agent_t *owner, esp_err_t error, esp_err_t cleanup_error)
{
    pet_ai_view_t view;
    (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
    if (view.turn_id != 0 && (view.state == PET_AI_LISTENING || view.state == PET_AI_THINKING ||
                              view.state == PET_AI_SPEAKING)) {
        const pet_ai_input_t failed = {
            .kind = PET_AI_AUDIO_FAILED, .turn_id = view.turn_id, .now_ms = monotonic_ms(),
        };
        pet_ai_runtime_step_t step;
        (void)pet_ai_runtime_handle(&owner->runtime, &failed, &step);
        if (cleanup_error == ESP_OK && step.cleanup_result != PET_AI_PORT_OK)
            cleanup_error = ESP_FAIL;
    }
    /* HAL 可能部分初始化/启停；始终由同一个任务尽力释放，失败不宣称已静音。 */
    const esp_err_t deinit = owner->config.audio->deinit();
    if (deinit == ESP_OK) owner->capturing = owner->playing = false;
    if (cleanup_error == ESP_OK) cleanup_error = deinit;
    if (owner->frontend_opened) {
        owner->frontend_opened = false;
        if (owner->config.frontend.close(owner->config.frontend.context) != PET_AI_PORT_OK &&
            cleanup_error == ESP_OK) cleanup_error = ESP_FAIL;
    }
    memset(owner->capture_pcm, 0, sizeof(owner->capture_pcm));
    memset(&owner->current, 0, sizeof(owner->current));
    xQueueReset(owner->playback);
    portENTER_CRITICAL(&status_lock);
    owner->status.ready = false;
    owner->status.faulted = true;
    owner->status.last_error = error;
    owner->status.cleanup_error = cleanup_error;
    portEXIT_CRITICAL(&status_lock);
    publish_status(owner);
}

static bool accept_result(agent_t *owner, pet_ai_runtime_result_t result,
                           const pet_ai_runtime_step_t *step)
{
    portENTER_CRITICAL(&status_lock);
    if (result == PET_AI_RUNTIME_STALE) ++owner->status.stale_items;
    if (result == PET_AI_RUNTIME_PORT_FAILED) owner->status.last_error = ESP_FAIL;
    portEXIT_CRITICAL(&status_lock);
    publish_status(owner);
    if (owner->hardware_error != ESP_OK || step->cleanup_result != PET_AI_PORT_OK ||
        (result == PET_AI_RUNTIME_PORT_FAILED && step->session_step.view.turn_id == 0)) {
        fault(owner, owner->hardware_error != ESP_OK ? owner->hardware_error : ESP_FAIL,
              step->cleanup_result == PET_AI_PORT_OK ? ESP_OK : ESP_FAIL);
        return false;
    }
    return true;
}

static bool dispatch(agent_t *owner, pet_ai_input_kind_t kind, uint32_t turn)
{
    const pet_ai_input_t input = {.kind = kind, .turn_id = turn, .now_ms = monotonic_ms()};
    pet_ai_runtime_step_t step;
    const pet_ai_runtime_result_t result = pet_ai_runtime_handle(&owner->runtime, &input, &step);
    return accept_result(owner, result, &step);
}

static bool apply_priority(agent_t *owner)
{
    portENTER_CRITICAL(&status_lock);
    const pet_ai_policy_t policy = owner->status.policy;
    const bool muted = policy.muted || policy.dnd;
    const bool cancel = owner->cancel_pending;
    const bool pressed = owner->status.ptt_requested;
    const uint64_t revision = owner->ptt_revision, epoch = owner->control_epoch;
    owner->cancel_pending = false;
    portEXIT_CRITICAL(&status_lock);
    pet_ai_view_t view;
    (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
    if (muted && view.state != PET_AI_MUTED && !dispatch(owner, PET_AI_MUTE, 0))
        return false;
    if (!muted && view.state == PET_AI_MUTED && !dispatch(owner, PET_AI_UNMUTE, 0))
        return false;
    if (cancel) {
        owner->ptt_turn = 0;
        if (!muted && !dispatch(owner, PET_AI_CANCEL, 0)) return false;
    }
    /* 空闲唤醒 RX 不属于活动会话，也必须真正停掉。 */
    if ((muted || policy.interrupted) && owner->capturing &&
        capture_stop(owner, view.turn_id) != PET_AI_PORT_OK) {
        fault(owner, owner->hardware_error, ESP_OK);
        return false;
    }
    if (revision != owner->seen_ptt_revision) {
        owner->seen_ptt_revision = revision;
        if (pressed && !muted && !policy.interrupted) {
            if (!dispatch(owner, PET_AI_TALK_PRESS, 0)) return false;
            (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
            if (view.state == PET_AI_LISTENING && view.push_to_talk) owner->ptt_turn = view.turn_id;
        } else if (!pressed && owner->ptt_turn != 0) {
            const uint32_t turn = owner->ptt_turn;
            owner->ptt_turn = 0;
            if (!dispatch(owner, PET_AI_TALK_RELEASE, turn)) return false;
        }
    }
    portENTER_CRITICAL(&status_lock);
    owner->applied_epoch = epoch;
    owner->status.policy_pending = owner->control_epoch != epoch;
    portEXIT_CRITICAL(&status_lock);
    return true;
}

static bool inhibited(agent_t *owner)
{
    portENTER_CRITICAL(&status_lock);
    const pet_ai_policy_t policy = owner->status.policy;
    /* 模型/采集回调期间到达的取消，同样不能让在途 PCM 开启或续传旧轮次。 */
    const bool blocked = policy.muted || policy.dnd || policy.interrupted || owner->cancel_pending;
    portEXIT_CRITICAL(&status_lock);
    return blocked;
}

static bool read_capture(agent_t *owner)
{
    if (owner->capture_fill < PET_AUDIO_CAPTURE_SAMPLES) {
        const size_t remaining = PET_AUDIO_CAPTURE_SAMPLES - owner->capture_fill;
        size_t samples = 0;
        const esp_err_t error = owner->config.audio->capture_read(
            owner->capture_pcm + owner->capture_fill, remaining, &samples, IO_TIMEOUT_MS);
        if (samples > remaining || (error != ESP_OK && error != ESP_ERR_TIMEOUT)) {
            fault(owner, samples > remaining ? ESP_ERR_INVALID_SIZE : error, ESP_OK);
            return false;
        }
        if (samples > 0) owner->capture_progress_ms = monotonic_ms();
        owner->capture_fill += samples;
    }
    if (inhibited(owner)) return apply_priority(owner);
    /* DMA 积压可立即返回，甚至与 press/上一帧处于同一毫秒。保留完整帧
     * 等时钟前进，不伪造未来时间，也不把有效 PCM 当作重复帧丢掉。 */
    if (owner->capture_fill == PET_AUDIO_CAPTURE_SAMPLES && monotonic_ms() >= owner->next_frame_ms) {
        pet_ai_runtime_step_t step;
        const pet_ai_runtime_result_t result = pet_ai_runtime_frame(
            &owner->runtime, owner->capture_pcm, owner->capture_fill, monotonic_ms(), &step);
        owner->next_frame_ms = monotonic_ms() + 1;
        owner->capture_fill = 0;
        memset(owner->capture_pcm, 0, sizeof(owner->capture_pcm));
        portENTER_CRITICAL(&status_lock);
        ++owner->status.captured_frames;
        portEXIT_CRITICAL(&status_lock);
        return accept_result(owner, result, &step);
    }
    if (monotonic_ms() - owner->capture_progress_ms > PET_AI_FRAME_GAP_MS) {
        fault(owner, ESP_ERR_TIMEOUT, ESP_OK);
        return false;
    }
    return true;
}

static bool write_playback(agent_t *owner, uint32_t turn, bool *progress)
{
    if (!owner->response_ended) {
        for (unsigned i=0;i<PLAYBACK_DEPTH &&
             owner->prebuffer.capacity-owner->prebuffer.count>=PET_AUDIO_PLAYBACK_SAMPLES;++i) {
            playback_t incoming;
            if (xQueueReceive(owner->playback,&incoming,0)!=pdTRUE) break;
            if (incoming.turn_id!=turn) {
                portENTER_CRITICAL(&status_lock); ++owner->status.stale_items; portEXIT_CRITICAL(&status_lock);
                continue;
            }
            if (incoming.finish) { owner->response_ended=true; break; }
            (void)pet_audio_buffer_push(&owner->prebuffer,incoming.pcm,incoming.samples);
            memset(&incoming,0,sizeof(incoming));
        }
    }
    if (!owner->playing) {
        if (owner->prebuffer.count>=PREBUFFER_SAMPLES || (owner->response_ended && owner->prebuffer.count)) {
            esp_err_t error=owner->config.audio->playback_start(PET_AUDIO_PLAYBACK_RATE);
            if (error!=ESP_OK) { owner->hardware_error=error; return dispatch(owner,PET_AI_AUDIO_FAILED,turn); }
            owner->playing=true; owner->next_silence_ms=monotonic_ms()+PET_AUDIO_FRAME_MS;
        } else if (owner->response_ended || monotonic_ms()-owner->playback_progress_ms>PLAYBACK_STALL_MS) {
            return dispatch(owner,PET_AI_TRANSPORT_FAILED,turn);
        } else return true;
    }
    if (owner->current.samples==0 && !owner->current.finish) {
        owner->current=(playback_t){.turn_id=turn};
        owner->current.samples=pet_audio_buffer_read(&owner->prebuffer,owner->current.pcm,PET_AUDIO_PLAYBACK_SAMPLES);
        owner->playback_offset=0;
        portENTER_CRITICAL(&status_lock); const uint8_t volume=owner->status.volume_pct; portEXIT_CRITICAL(&status_lock);
        (void)pet_audio_pcm_volume(owner->current.pcm,owner->current.pcm,owner->current.samples,volume);
        if (owner->response_ended && !owner->prebuffer.count) {
            if (!owner->current.samples) owner->current.finish=true;
            else {
                size_t n=owner->current.samples<240 ? owner->current.samples : 240;
                for (size_t i=0;i<n;++i) {
                    size_t at=owner->current.samples-n+i;
                    owner->current.pcm[at]=(int16_t)((int32_t)owner->current.pcm[at]*(int32_t)(n-i-1)/(int32_t)n);
                }
            }
        } else if (!owner->current.samples && monotonic_ms()>=owner->next_silence_ms) {
            owner->current.samples=PET_AUDIO_PLAYBACK_SAMPLES; owner->current.silence=true;
            portENTER_CRITICAL(&status_lock); ++owner->status.playback_underruns; portEXIT_CRITICAL(&status_lock);
        }
    }
    if (owner->current.finish) {
        if (!owner->submitted_audio) return dispatch(owner, PET_AI_TRANSPORT_FAILED, turn);
        const esp_err_t error = owner->config.audio->playback_drain(120);
        if (error != ESP_OK) owner->hardware_error = error;
        return dispatch(owner, error == ESP_OK ? PET_AI_PLAYBACK_DRAINED : PET_AI_AUDIO_FAILED, turn);
    }
    if (owner->current.samples > 0) {
        const bool silence=owner->current.silence;
        const size_t remaining = owner->current.samples - owner->playback_offset;
        size_t written = 0;
        const esp_err_t error = owner->config.audio->playback_write(
            owner->current.pcm + owner->playback_offset, remaining, &written, IO_TIMEOUT_MS);
        if (written > remaining || (error != ESP_OK && error != ESP_ERR_TIMEOUT)) {
            owner->hardware_error = written > remaining ? ESP_ERR_INVALID_SIZE : error;
            return dispatch(owner, PET_AI_AUDIO_FAILED, turn);
        }
        if (written > 0) {
            *progress=true;
            if (!owner->current.silence) {
                owner->submitted_audio=true;
                owner->playback_progress_ms=monotonic_ms();
            }
            owner->next_silence_ms=monotonic_ms()+PET_AUDIO_FRAME_MS;
            owner->last_output=owner->current.pcm[owner->playback_offset+written-1];
            owner->playback_offset += written;
            pet_audio_level_t level={0};
            (void)pet_audio_pcm_measure(owner->current.pcm,owner->current.samples,&level);
            portENTER_CRITICAL(&status_lock);
            if (!silence) owner->status.submitted_samples += written;
            owner->status.playback_level_pct=level.level_pct;
            portEXIT_CRITICAL(&status_lock);
        }
        if (owner->playback_offset == owner->current.samples)
            memset(&owner->current, 0, sizeof(owner->current));
        if (!silence && monotonic_ms() - owner->playback_progress_ms > PET_AI_FRAME_GAP_MS) {
            owner->hardware_error = ESP_ERR_TIMEOUT;
            return dispatch(owner, PET_AI_AUDIO_FAILED, turn);
        }
    }
    if (monotonic_ms() - owner->playback_progress_ms > PLAYBACK_STALL_MS) {
        return dispatch(owner, PET_AI_TRANSPORT_FAILED, turn);
    }
    return true;
}

static bool notification_tick(agent_t *owner)
{
    portENTER_CRITICAL(&status_lock);
    pet_message_sound_t sound=owner->notification;
    bool blocked=owner->status.policy.muted||owner->status.policy.dnd||owner->cancel_pending;
    bool interrupt=owner->status.policy.interrupted;
    bool critical=owner->notification_critical;
    uint64_t queued=owner->notification_ms;
    portEXIT_CRITICAL(&status_lock);
    pet_ai_view_t view;(void)pet_ai_runtime_snapshot(&owner->runtime,&view);
    bool active=view.state==PET_AI_LISTENING||view.state==PET_AI_THINKING||view.state==PET_AI_SPEAKING;
    if (blocked||active||(interrupt&&!critical)) {
        if (owner->tone_active) {
            esp_err_t error=owner->config.audio->playback_stop();
            if (error!=ESP_OK) {fault(owner,error,ESP_OK);return false;}
            owner->tone_active=owner->playing=false;
        }
        return true;
    }
    if (sound==PET_MESSAGE_SOUND_NONE || monotonic_ms()-queued>3000 ||
        (owner->tone_active&&sound!=owner->tone_sound)) {
        if(owner->tone_active) {
            esp_err_t error=owner->config.audio->playback_stop();
            if(error!=ESP_OK){fault(owner,error,ESP_OK);return false;}
            owner->tone_active=owner->playing=false;
        }
        if(sound==PET_MESSAGE_SOUND_NONE||monotonic_ms()-queued>3000)return true;
    }
    if (!owner->tone_active) {
        if (owner->capturing && capture_stop(owner,view.turn_id)!=PET_AI_PORT_OK) {fault(owner,owner->hardware_error,ESP_OK);return false;}
        esp_err_t error=owner->config.audio->playback_start(PET_AUDIO_PLAYBACK_RATE);
        if (error!=ESP_OK) {fault(owner,error,ESP_OK);return false;}
        owner->tone_offset=0;owner->tone_active=owner->playing=true;
        owner->tone_sound=sound;owner->tone_progress_ms=monotonic_ms();
    }
    const uint32_t length=sound==PET_MESSAGE_SOUND_ALERT?5760:3840;
    int16_t pcm[PET_AUDIO_PLAYBACK_SAMPLES];
    portENTER_CRITICAL(&status_lock);uint8_t volume=owner->status.volume_pct;portEXIT_CRITICAL(&status_lock);
    size_t count=length-owner->tone_offset;if (count>PET_AUDIO_PLAYBACK_SAMPLES) count=PET_AUDIO_PLAYBACK_SAMPLES;
    for (size_t i=0;i<count;++i) {
        uint32_t at=owner->tone_offset+(uint32_t)i;
        float fade=at<240?(float)at/240:length-at<240?(float)(length-at)/240:1;
        unsigned frequency=at<length/2?880:1175;
        pcm[i]=(int16_t)(sinf((float)at*(6.2831853f*(float)frequency/PET_AUDIO_PLAYBACK_RATE))*3200*fade*(float)volume/100);
    }
    size_t written=0;esp_err_t error=owner->config.audio->playback_write(pcm,count,&written,IO_TIMEOUT_MS);
    if (written>count||error!=ESP_OK) {fault(owner,error==ESP_OK?ESP_FAIL:error,ESP_OK);return false;}
    owner->tone_offset+=(uint32_t)written;
    if(written)owner->tone_progress_ms=monotonic_ms();
    else if(monotonic_ms()-owner->tone_progress_ms>=100){fault(owner,ESP_ERR_TIMEOUT,ESP_OK);return false;}
    if (owner->tone_offset>=length) {
        error=owner->config.audio->playback_drain(120);
        if (error==ESP_OK) error=owner->config.audio->playback_stop();
        if (error!=ESP_OK) {fault(owner,error,ESP_OK);return false;}
        owner->tone_active=owner->playing=false;
        portENTER_CRITICAL(&status_lock);
        if(owner->notification_ms==queued&&owner->notification==sound)owner->notification=PET_MESSAGE_SOUND_NONE;
        portEXIT_CRITICAL(&status_lock);
    }
    return true;
}

static void agent_task(void *context)
{
    agent_t *owner = context;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    const pet_ai_runtime_sink_t sink = {
        .context = owner, .begin_turn = begin_turn, .forward_pcm = forward_pcm,
        .end_input = end_input, .cancel_turn = cancel_turn,
        .start_capture = capture_start, .stop_capture = capture_stop,
        .start_playback = playback_start, .stop_playback = playback_stop, .clear_audio = clear_audio,
    };
    const pet_ai_runtime_frontend_t frontend = {
        .context = owner,
        .detect_wake = owner->config.frontend.detect_wake != NULL ? frontend_wake : NULL,
        .classify_vad = owner->config.frontend.classify_vad != NULL ? frontend_vad : NULL,
        .preprocess = owner->config.frontend.preprocess ? frontend_preprocess : NULL,
    };
    (void)pet_ai_runtime_init(&owner->runtime, NULL, &sink, &frontend, monotonic_ms());
    const esp_err_t error = owner->config.audio->init();
    if (error != ESP_OK) {
        fault(owner, error, ESP_OK);
        vTaskDelete(NULL);
        return;
    }
    if (owner->config.frontend.open != NULL) {
        if (owner->config.frontend.open(owner->config.frontend.context) != PET_AI_PORT_OK) {
            fault(owner, ESP_FAIL, ESP_OK);
            vTaskDelete(NULL);
            return;
        }
        owner->frontend_opened = true;
    }
    portENTER_CRITICAL(&status_lock);
    owner->status.ready = true;
    portEXIT_CRITICAL(&status_lock);
    for (;;) {
        const uint64_t started_ms = monotonic_ms();
        portENTER_CRITICAL(&status_lock);
        bool tone_blocked=owner->status.policy.muted||owner->status.policy.dnd||owner->cancel_pending||
            (owner->status.policy.interrupted&&!owner->notification_critical);
        portEXIT_CRITICAL(&status_lock);
        if (owner->tone_active && tone_blocked) {
            esp_err_t stopped=owner->config.audio->playback_stop();
            if (stopped!=ESP_OK) {fault(owner,stopped,ESP_OK);break;}
            owner->tone_active=owner->playing=false;
            portENTER_CRITICAL(&status_lock);owner->notification=PET_MESSAGE_SOUND_NONE;portEXIT_CRITICAL(&status_lock);
        }
        if (!apply_priority(owner)) break;
        control_t control;
        if (xQueueReceive(owner->controls, &control, 0) == pdTRUE) {
            portENTER_CRITICAL(&status_lock);
            const pet_ai_policy_t policy = owner->status.policy;
            const bool start = control.kind == PET_AI_TALK_PRESS || control.kind == PET_AI_WAKE_DETECTED;
            const bool stale = control.epoch != owner->control_epoch ||
                (start && (policy.muted || policy.dnd || policy.interrupted));
            if (stale) ++owner->status.stale_items;
            portEXIT_CRITICAL(&status_lock);
            if (!stale && !dispatch(owner, control.kind, control.turn_id)) break;
        }
        if (!dispatch(owner, PET_AI_TICK, 0)) break;
        pet_ai_view_t view;
        (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
        const bool wake_capture = owner->config.frontend.detect_wake != NULL &&
            !inhibited(owner) && !owner->tone_active && (view.state == PET_AI_IDLE || view.state == PET_AI_ERROR);
        if (owner->capturing && !wake_capture && view.state != PET_AI_LISTENING &&
            capture_stop(owner, view.turn_id) != PET_AI_PORT_OK) {
            fault(owner, owner->hardware_error, ESP_OK);
            break;
        }
        if (wake_capture && !owner->capturing && capture_start(owner, view.turn_id) != PET_AI_PORT_OK) {
            fault(owner, owner->hardware_error, ESP_OK);
            break;
        }
        if (owner->capturing && !read_capture(owner)) break;
        if (!apply_priority(owner)) break;
        (void)pet_ai_runtime_snapshot(&owner->runtime, &view);
        bool playback_progress = false;
        if (view.state == PET_AI_SPEAKING && !write_playback(owner, view.turn_id, &playback_progress))
            break;
        if (!notification_tick(owner)) break;
#ifdef ESP_PLATFORM
        uint32_t stack=uxTaskGetStackHighWaterMark(NULL);
        portENTER_CRITICAL(&status_lock);owner->status.owner_stack_free=stack;portEXIT_CRITICAL(&status_lock);
#endif
        publish_status(owner);
        const uint64_t elapsed = monotonic_ms() - started_ms;
        if (!playback_progress && elapsed < PET_AUDIO_FRAME_MS) {
            TickType_t ticks = pdMS_TO_TICKS(PET_AUDIO_FRAME_MS - elapsed);
            vTaskDelay(ticks > 0 ? ticks : 1);
        } else {
            taskYIELD();
        }
    }
    vTaskDelete(NULL);
}

static bool valid_audio(const pet_audio_device_t *audio)
{
    return audio != NULL && audio->init && audio->deinit && audio->capture_start &&
        audio->capture_read && audio->capture_stop && audio->playback_start &&
        audio->playback_write && audio->playback_stop && audio->playback_drain;
}

esp_err_t pet_ai_agent_start(const pet_ai_agent_config_t *config)
{
    if (config == NULL || !valid_audio(config->audio) || config->volume_pct > 100 ||
        (!config->notifications_only && (!config->transport.begin_turn || !config->transport.forward_pcm ||
        !config->transport.end_input || !config->transport.cancel_turn)) ||
        (config->notifications_only&&(config->frontend.open||config->frontend.detect_wake||config->frontend.classify_vad||config->frontend.preprocess)) ||
        ((config->frontend.open == NULL) != (config->frontend.close == NULL)))
        return ESP_ERR_INVALID_ARG;
    if (agent != NULL) return ESP_ERR_INVALID_STATE; /* 组合根启动，不并发调用 start。 */
    agent_t *owner = heap_caps_malloc(sizeof(*owner), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (owner == NULL) return ESP_ERR_NO_MEM;
    memset(owner, 0, sizeof(*owner));
    owner->config = *config;
    (void)pet_audio_buffer_init(&owner->prebuffer,owner->prebuffer_pcm,PREBUFFER_SAMPLES+PET_AUDIO_PLAYBACK_SAMPLES);
    owner->status.policy = config->initial_policy;
    owner->status.volume_pct = config->volume_pct;
    owner->status.voice_enabled = !config->notifications_only;
    owner->control_epoch = 1;
    owner->status.policy_pending = true;
    owner->controls = xQueueCreate(CONTROL_DEPTH, sizeof(control_t));
    owner->playback = xQueueCreate(PLAYBACK_DEPTH, sizeof(playback_t));
    TaskHandle_t task = NULL;
    if (owner->controls == NULL || owner->playback == NULL ||
        xTaskCreate(agent_task, "pet_ai_agent", config->frontend.open != NULL ? 8192 : 6144,
                     owner, 4, &task) != pdPASS) {
        if (owner->controls != NULL) vQueueDelete(owner->controls);
        if (owner->playback != NULL) vQueueDelete(owner->playback);
        heap_caps_free(owner);
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&status_lock);
    agent = owner;
    portEXIT_CRITICAL(&status_lock);
    xTaskNotifyGive(task);
    return ESP_OK;
}

static agent_t *available_agent(void)
{
    portENTER_CRITICAL(&status_lock);
    agent_t *owner = agent;
    if (owner != NULL && owner->status.faulted) owner = NULL;
    portEXIT_CRITICAL(&status_lock);
    return owner;
}

esp_err_t pet_ai_agent_post(pet_ai_input_kind_t kind, uint32_t turn_id)
{
    if (kind < PET_AI_WAKE_DETECTED || kind >= PET_AI_INPUT_COUNT ||
        kind == PET_AI_PLAYBACK_DRAINED || kind == PET_AI_TICK) return ESP_ERR_INVALID_ARG;
    const bool scoped = kind == PET_AI_TALK_RELEASE || kind == PET_AI_RESPONSE_BEGIN ||
        kind == PET_AI_AUDIO_FAILED || kind == PET_AI_TRANSPORT_FAILED;
    if (scoped != (turn_id != 0)) return ESP_ERR_INVALID_ARG;
    agent_t *owner = available_agent();
    if (owner == NULL) return ESP_ERR_INVALID_STATE;
    if(owner->config.notifications_only&&kind!=PET_AI_MUTE&&kind!=PET_AI_UNMUTE&&kind!=PET_AI_CANCEL)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    const pet_ai_policy_t policy = owner->status.policy;
    if ((kind == PET_AI_TALK_PRESS || kind == PET_AI_WAKE_DETECTED) &&
        (policy.muted || policy.dnd || policy.interrupted)) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (kind == PET_AI_MUTE || kind == PET_AI_UNMUTE || kind == PET_AI_CANCEL) {
        if (kind == PET_AI_CANCEL || owner->status.policy.muted != (kind == PET_AI_MUTE)) {
            if (kind != PET_AI_CANCEL) owner->status.policy.muted = kind == PET_AI_MUTE;
            owner->notification=PET_MESSAGE_SOUND_NONE;
            ++owner->control_epoch;
            owner->cancel_pending = owner->status.policy_pending = true;
            owner->status.ptt_requested = false;
            ++owner->ptt_revision;
        }
        portEXIT_CRITICAL(&status_lock);
        return ESP_OK;
    }
    const control_t control = {.kind = kind, .turn_id = turn_id, .epoch = owner->control_epoch};
    portEXIT_CRITICAL(&status_lock);
    if (xQueueSend(owner->controls, &control, 0) == pdTRUE) return ESP_OK;
    portENTER_CRITICAL(&status_lock);
    ++owner->status.rejected_commands;
    portEXIT_CRITICAL(&status_lock);
    return ESP_ERR_TIMEOUT;
}

static void replace_policy_locked(agent_t *owner, const pet_ai_policy_t *policy)
{
    const pet_ai_policy_t old = owner->status.policy;
    if (old.muted != policy->muted || old.dnd != policy->dnd || old.interrupted != policy->interrupted) {
        owner->notification=PET_MESSAGE_SOUND_NONE;
        owner->status.policy = *policy;
        ++owner->control_epoch;
        owner->cancel_pending = owner->status.policy_pending = true;
        owner->status.ptt_requested = false;
        ++owner->ptt_revision;
    }
}

esp_err_t pet_ai_agent_set_policy(const pet_ai_policy_t *policy)
{
    if (policy == NULL) return ESP_ERR_INVALID_ARG;
    agent_t *owner = available_agent();
    if (owner == NULL) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    replace_policy_locked(owner, policy);
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

static esp_err_t update_gate(bool interrupt, bool enabled)
{
    agent_t *owner = available_agent();
    if (owner == NULL) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    pet_ai_policy_t policy = owner->status.policy;
    if (interrupt) policy.interrupted = enabled;
    else policy.dnd = enabled;
    replace_policy_locked(owner, &policy);
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

esp_err_t pet_ai_agent_set_dnd(bool enabled) { return update_gate(false, enabled); }
esp_err_t pet_ai_agent_set_interrupted(bool enabled) { return update_gate(true, enabled); }
esp_err_t pet_ai_agent_set_volume(uint8_t volume)
{
    if (volume > 100) return ESP_ERR_INVALID_ARG;
    agent_t *owner = available_agent();
    if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock); owner->status.volume_pct = volume; portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

esp_err_t pet_ai_agent_ptt(bool pressed)
{
    agent_t *owner = available_agent();
    if (owner == NULL) return ESP_ERR_INVALID_STATE;
    if(pressed&&owner->config.notifications_only)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    const pet_ai_policy_t policy = owner->status.policy;
    if (pressed && (!owner->status.ready || policy.muted || policy.dnd || policy.interrupted)) {
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (owner->status.ptt_requested != pressed) {
        owner->status.ptt_requested = pressed;
        ++owner->ptt_revision;
    }
    portEXIT_CRITICAL(&status_lock);
    return ESP_OK;
}

esp_err_t pet_ai_agent_snapshot(pet_ai_agent_snapshot_t *snapshot)
{
    if (snapshot == NULL) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&status_lock);
    const agent_t *owner = agent;
    if (owner != NULL) *snapshot = owner->status;
    portEXIT_CRITICAL(&status_lock);
    return owner == NULL ? ESP_ERR_INVALID_STATE : ESP_OK;
}

esp_err_t pet_ai_agent_play(uint32_t turn_id, const int16_t *pcm, size_t samples, bool finish)
{
    if (turn_id == 0 || (finish && (samples != 0 || pcm != NULL)) ||
        (!finish && (pcm == NULL || samples == 0 || samples > PET_AUDIO_PLAYBACK_SAMPLES)))
        return ESP_ERR_INVALID_ARG;
    agent_t *owner = available_agent();
    if (owner == NULL) return ESP_ERR_INVALID_STATE;
    if(owner->config.notifications_only)return ESP_ERR_INVALID_STATE;
    playback_t item = {.turn_id = turn_id, .samples = samples, .finish = finish};
    if (!finish) memcpy(item.pcm, pcm, samples * sizeof(*pcm));
    if (xQueueSend(owner->playback, &item, 0) == pdTRUE) return ESP_OK;
    portENTER_CRITICAL(&status_lock);
    ++owner->status.rejected_commands;
    portEXIT_CRITICAL(&status_lock);
    return ESP_ERR_TIMEOUT;
}

esp_err_t pet_ai_agent_text(uint32_t turn,pet_ai_text_role_t role,const char *text)
{
    if (!turn || !text || (role!=PET_AI_TEXT_USER && role!=PET_AI_TEXT_ASSISTANT) || strlen(text)>PET_AI_TEXT_MAX)
        return ESP_ERR_INVALID_ARG;
    agent_t *owner=available_agent();
    if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    pet_ai_policy_t policy=owner->status.policy;
    esp_err_t error=turn!=owner->status.view.turn_id || policy.muted || policy.dnd || policy.interrupted ||
        owner->cancel_pending || owner->control_epoch!=owner->applied_epoch ?
        ESP_ERR_INVALID_STATE : ESP_OK;
    if (error==ESP_OK) {
        char *out=role==PET_AI_TEXT_USER ? owner->status.transcript : owner->status.reply;
        memset(out,0,PET_AI_TEXT_MAX+1); memcpy(out,text,strlen(text));
    }
    portEXIT_CRITICAL(&status_lock);
    return error;
}

esp_err_t pet_ai_agent_context(pet_ai_context_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    agent_t *owner=available_agent();
    if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    pet_ai_policy_t policy=owner->status.policy;
    bool blocked=policy.muted||policy.dnd||policy.interrupted||owner->cancel_pending||
        owner->control_epoch!=owner->applied_epoch;
    if(blocked)pet_ai_context_clear(out);else *out=owner->context;
    portEXIT_CRITICAL(&status_lock);return blocked?ESP_ERR_INVALID_STATE:ESP_OK;
}
esp_err_t pet_ai_agent_forget(void)
{
    agent_t *owner=available_agent();
    if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    pet_ai_context_clear(&owner->context); owner->context_updated_ms=0;
    memset(owner->status.transcript,0,sizeof(owner->status.transcript));
    memset(owner->status.reply,0,sizeof(owner->status.reply));
    owner->cancel_pending=owner->status.policy_pending=true;
    owner->status.ptt_requested=false; ++owner->ptt_revision; ++owner->control_epoch;
    portEXIT_CRITICAL(&status_lock); return ESP_OK;
}

esp_err_t pet_ai_agent_notify(pet_message_sound_t sound,bool critical)
{
    if (sound!=PET_MESSAGE_SOUND_CHIME && sound!=PET_MESSAGE_SOUND_ALERT) return ESP_ERR_INVALID_ARG;
    agent_t *owner=available_agent();if (!owner) return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&status_lock);
    esp_err_t error=!owner->status.ready||owner->status.policy.muted||owner->status.policy.dnd?ESP_ERR_INVALID_STATE:ESP_OK;
    if (error==ESP_OK) {owner->notification=sound;owner->notification_critical=critical;owner->notification_ms=monotonic_ms();}
    portEXIT_CRITICAL(&status_lock);return error;
}
