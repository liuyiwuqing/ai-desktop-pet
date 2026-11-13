#include "svc_ai_agent/runtime.h"

#include <string.h>

static pet_ai_runtime_result_t map_session_result(pet_ai_result_t result)
{
    switch (result) {
    case PET_AI_OK: return PET_AI_RUNTIME_OK;
    case PET_AI_IGNORED: return PET_AI_RUNTIME_IGNORED;
    case PET_AI_STALE: return PET_AI_RUNTIME_STALE;
    case PET_AI_NOT_READY: return PET_AI_RUNTIME_NOT_READY;
    case PET_AI_INVALID:
    case PET_AI_ID_EXHAUSTED:
    default: return PET_AI_RUNTIME_INVALID;
    }
}

static pet_ai_runtime_result_t prepare(const pet_ai_runtime_t *runtime,
                                       pet_ai_runtime_step_t *step)
{
    if (step == NULL) return PET_AI_RUNTIME_INVALID;
    *step = (pet_ai_runtime_step_t){0};
    if (runtime == NULL || !runtime->ready) return PET_AI_RUNTIME_NOT_READY;
    if (runtime->applying) return PET_AI_RUNTIME_BUSY;
    return PET_AI_RUNTIME_OK;
}

static pet_ai_port_result_t cleanup_actions(pet_ai_runtime_t *runtime, uint32_t actions,
                                            uint32_t turn_id,
                                            pet_ai_input_kind_t *failure_kind)
{
    const struct {
        uint32_t action;
        pet_ai_runtime_void_fn function;
        pet_ai_input_kind_t failure_kind;
    } calls[] = {
        {PET_AI_ACTION_CANCEL_TURN, runtime->sink.cancel_turn, PET_AI_TRANSPORT_FAILED},
        {PET_AI_ACTION_STOP_CAPTURE, runtime->sink.stop_capture, PET_AI_AUDIO_FAILED},
        {PET_AI_ACTION_STOP_PLAYBACK, runtime->sink.stop_playback, PET_AI_AUDIO_FAILED},
        {PET_AI_ACTION_CLEAR_AUDIO, runtime->sink.clear_audio, PET_AI_AUDIO_FAILED},
    };
    pet_ai_port_result_t result = PET_AI_PORT_OK;
    /* 清理端口彼此独立；某一项失败也要继续尝试其余项，且不递归重试。 */
    for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) {
        if (!(actions & calls[i].action)) continue;
        if (calls[i].function(runtime->sink.context, turn_id) != PET_AI_PORT_OK &&
            result == PET_AI_PORT_OK) {
            result = PET_AI_PORT_FAILED;
            if (failure_kind != NULL) *failure_kind = calls[i].failure_kind;
        }
    }
    return result;
}

static pet_ai_runtime_result_t fail_session(pet_ai_runtime_t *runtime,
                                            pet_ai_input_kind_t kind, uint32_t turn_id,
                                            uint64_t now_ms, bool compensate,
                                            pet_ai_runtime_step_t *step)
{
    const pet_ai_input_t input = {.kind = kind, .turn_id = turn_id, .now_ms = now_ms};
    step->session_result = pet_ai_session_handle(&runtime->session, &input, &step->session_step);
    step->port_result = PET_AI_PORT_FAILED;
    if (compensate) {
        const bool applying = runtime->applying;
        runtime->applying = true;
        const uint32_t cleanup_turn = step->session_step.cancelled_turn_id != 0 ?
            step->session_step.cancelled_turn_id : turn_id;
        step->cleanup_result = cleanup_actions(runtime, step->session_step.actions,
                                                cleanup_turn, NULL);
        runtime->applying = applying;
    }
    return PET_AI_RUNTIME_PORT_FAILED;
}

static pet_ai_runtime_result_t invoke_void(pet_ai_runtime_t *runtime,
                                           pet_ai_runtime_void_fn function,
                                           uint32_t callback_turn_id,
                                           uint32_t failure_turn_id,
                                           pet_ai_input_kind_t failure_kind,
                                           uint64_t now_ms,
                                           pet_ai_runtime_step_t *step)
{
    const pet_ai_port_result_t result = function(runtime->sink.context, callback_turn_id);
    if (result == PET_AI_PORT_OK) return PET_AI_RUNTIME_OK;
    return fail_session(runtime, failure_kind, failure_turn_id, now_ms, true, step);
}

static pet_ai_runtime_result_t apply(pet_ai_runtime_t *runtime,
                                     const pet_ai_step_t *session_step,
                                     const int16_t *pcm, size_t samples,
                                     uint64_t now_ms,
                                     pet_ai_runtime_step_t *step)
{
    const uint32_t actions = session_step->actions;
    const uint32_t turn_id = session_step->view.turn_id;
    const uint32_t cleanup_turn = (actions & PET_AI_ACTION_CANCEL_TURN) &&
        session_step->cancelled_turn_id != 0 ? session_step->cancelled_turn_id : turn_id;
    runtime->applying = true;

    /* 先清理旧轮次，再开启新轮次；顺序是运行任务的唯一执行契约。 */
    pet_ai_input_kind_t cleanup_failure = PET_AI_AUDIO_FAILED;
    if (cleanup_actions(runtime, actions, cleanup_turn, &cleanup_failure) != PET_AI_PORT_OK) {
        /* 新轮次尚未启动；回送当前轮次错误，但不重复清理旧端口。 */
        fail_session(runtime, cleanup_failure, turn_id, now_ms, false, step);
        step->cleanup_result = PET_AI_PORT_FAILED;
        runtime->applying = false;
        return PET_AI_RUNTIME_PORT_FAILED;
    }
    if (actions & PET_AI_ACTION_BEGIN_TURN) {
        if (invoke_void(runtime, runtime->sink.begin_turn, turn_id, turn_id, PET_AI_TRANSPORT_FAILED,
                        now_ms, step) != PET_AI_RUNTIME_OK) {
            runtime->applying = false;
            return PET_AI_RUNTIME_PORT_FAILED;
        }
    }
    if (actions & PET_AI_ACTION_START_CAPTURE) {
        if (invoke_void(runtime, runtime->sink.start_capture, turn_id, turn_id, PET_AI_AUDIO_FAILED,
                        now_ms, step) != PET_AI_RUNTIME_OK) {
            runtime->applying = false;
            return PET_AI_RUNTIME_PORT_FAILED;
        }
    }
    if (actions & PET_AI_ACTION_FORWARD_PCM) {
        if (pcm == NULL || samples != PET_AUDIO_CAPTURE_SAMPLES) {
            runtime->applying = false;
            return fail_session(runtime, PET_AI_TRANSPORT_FAILED, turn_id, now_ms, true, step);
        }
        const pet_ai_port_result_t result = runtime->sink.forward_pcm(
            runtime->sink.context, turn_id, pcm, samples);
        if (result != PET_AI_PORT_OK) {
            runtime->applying = false;
            return fail_session(runtime, PET_AI_TRANSPORT_FAILED, turn_id, now_ms, true, step);
        }
    }
    if (actions & PET_AI_ACTION_END_INPUT) {
        if (invoke_void(runtime, runtime->sink.end_input, turn_id, turn_id, PET_AI_TRANSPORT_FAILED,
                        now_ms, step) != PET_AI_RUNTIME_OK) {
            runtime->applying = false;
            return PET_AI_RUNTIME_PORT_FAILED;
        }
    }
    if (actions & PET_AI_ACTION_START_PLAYBACK) {
        if (invoke_void(runtime, runtime->sink.start_playback, turn_id, turn_id, PET_AI_AUDIO_FAILED,
                        now_ms, step) != PET_AI_RUNTIME_OK) {
            runtime->applying = false;
            return PET_AI_RUNTIME_PORT_FAILED;
        }
    }
    runtime->applying = false;
    step->session_step = *session_step;
    step->session_result = PET_AI_OK;
    step->port_result = PET_AI_PORT_OK;
    return PET_AI_RUNTIME_OK;
}

static pet_ai_runtime_result_t dispatch(pet_ai_runtime_t *runtime,
                                        const pet_ai_input_t *input,
                                        pet_ai_runtime_step_t *step)
{
    step->session_result = pet_ai_session_handle(&runtime->session, input, &step->session_step);
    const pet_ai_runtime_result_t mapped = map_session_result(step->session_result);
    if (mapped != PET_AI_RUNTIME_OK) return mapped;
    return apply(runtime, &step->session_step, NULL, 0, input->now_ms, step);
}

pet_ai_runtime_result_t pet_ai_runtime_init(pet_ai_runtime_t *runtime,
                                            const pet_ai_session_config_t *session_config,
                                            const pet_ai_runtime_sink_t *sink,
                                            const pet_ai_runtime_frontend_t *frontend,
                                            uint64_t now_ms)
{
    if (runtime == NULL || sink == NULL || sink->begin_turn == NULL ||
        sink->start_capture == NULL || sink->stop_capture == NULL || sink->forward_pcm == NULL ||
        sink->end_input == NULL || sink->cancel_turn == NULL || sink->start_playback == NULL ||
        sink->stop_playback == NULL || sink->clear_audio == NULL)
        return PET_AI_RUNTIME_INVALID;
    *runtime = (pet_ai_runtime_t){0};
    if (pet_ai_session_init(&runtime->session, session_config, now_ms) != PET_AI_OK)
        return PET_AI_RUNTIME_INVALID;
    runtime->sink = *sink;
    if (frontend != NULL) runtime->frontend = *frontend;
    runtime->ready = true;
    return PET_AI_RUNTIME_OK;
}

bool pet_ai_runtime_snapshot(const pet_ai_runtime_t *runtime, pet_ai_view_t *view)
{
    return runtime != NULL && runtime->ready && !runtime->applying &&
           pet_ai_session_snapshot(&runtime->session, view);
}

pet_ai_runtime_result_t pet_ai_runtime_handle(pet_ai_runtime_t *runtime,
                                               const pet_ai_input_t *input,
                                               pet_ai_runtime_step_t *step)
{
    const pet_ai_runtime_result_t prepared = prepare(runtime, step);
    if (prepared != PET_AI_RUNTIME_OK) return prepared;
    if (input == NULL) return PET_AI_RUNTIME_INVALID;
    return dispatch(runtime, input, step);
}

pet_ai_runtime_result_t pet_ai_runtime_frame(pet_ai_runtime_t *runtime,
                                              const int16_t *pcm, size_t samples,
                                              uint64_t now_ms,
                                              pet_ai_runtime_step_t *step)
{
    const pet_ai_runtime_result_t prepared = prepare(runtime, step);
    if (prepared != PET_AI_RUNTIME_OK) return prepared;
    if (pcm == NULL || samples != PET_AUDIO_CAPTURE_SAMPLES) return PET_AI_RUNTIME_INVALID;

    pet_ai_view_t view;
    if (!pet_ai_session_snapshot(&runtime->session, &view)) return PET_AI_RUNTIME_NOT_READY;
    step->session_step.view = view;
    /* 先挡住乱序帧，再调用唤醒/VAD 适配器，避免旧 PCM 触发外部副作用。 */
    if (now_ms < runtime->session.last_now_ms) {
        step->session_result = PET_AI_STALE;
        step->port_result = PET_AI_PORT_OK;
        return PET_AI_RUNTIME_STALE;
    }
    if (view.state == PET_AI_MUTED || view.state == PET_AI_THINKING || view.state == PET_AI_SPEAKING) {
        step->session_result = PET_AI_IGNORED;
        step->port_result = PET_AI_PORT_OK;
        return PET_AI_RUNTIME_IGNORED;
    }

    int16_t processed[PET_AUDIO_CAPTURE_SAMPLES];
    if (runtime->frontend.preprocess) {
        pet_ai_port_result_t result=runtime->frontend.preprocess(runtime->frontend.context,pcm,samples,now_ms,processed);
        if (result!=PET_AI_PORT_OK) {
            if (view.turn_id) return fail_session(runtime,PET_AI_AUDIO_FAILED,view.turn_id,now_ms,true,step);
            step->port_result=result;return PET_AI_RUNTIME_PORT_FAILED;
        }
        pcm=processed;
    }
    if (view.state == PET_AI_IDLE || view.state == PET_AI_ERROR) {
        bool detected = false;
        if (runtime->frontend.detect_wake == NULL) {
            step->session_result = PET_AI_IGNORED;
            step->port_result = PET_AI_PORT_OK;
            return PET_AI_RUNTIME_IGNORED;
        }
        const pet_ai_port_result_t result = runtime->frontend.detect_wake(
            runtime->frontend.context, pcm, samples, now_ms, &detected);
        if (result != PET_AI_PORT_OK) {
            if (view.turn_id == 0) {
                step->session_result = PET_AI_INVALID;
                step->port_result = PET_AI_PORT_FAILED;
                return PET_AI_RUNTIME_PORT_FAILED;
            }
            return fail_session(runtime, PET_AI_AUDIO_FAILED, view.turn_id, now_ms, true, step);
        }
        if (!detected) {
            step->session_result = PET_AI_IGNORED;
            step->port_result = PET_AI_PORT_OK;
            return PET_AI_RUNTIME_IGNORED;
        }
        const pet_ai_input_t wake = {.kind = PET_AI_WAKE_DETECTED, .now_ms = now_ms};
        return dispatch(runtime, &wake, step);
    }

    pet_ai_activity_t activity = PET_AI_ACTIVITY_ENERGY;
    if (runtime->frontend.classify_vad != NULL) {
        const pet_ai_port_result_t result = runtime->frontend.classify_vad(
            runtime->frontend.context, pcm, samples, now_ms, &activity);
        if (result != PET_AI_PORT_OK || activity < PET_AI_ACTIVITY_ENERGY ||
            activity > PET_AI_ACTIVITY_SILENCE)
            return fail_session(runtime, PET_AI_AUDIO_FAILED, view.turn_id, now_ms, true, step);
    }
    const pet_ai_audio_input_t audio = {
        .turn_id = view.turn_id, .now_ms = now_ms, .pcm = pcm, .samples = samples,
        .activity = activity,
    };
    step->session_result = pet_ai_session_audio(&runtime->session, &audio, &step->session_step);
    const pet_ai_runtime_result_t mapped = map_session_result(step->session_result);
    if (mapped != PET_AI_RUNTIME_OK) return mapped;
    return apply(runtime, &step->session_step, pcm, samples, now_ms, step);
}
