#include <assert.h>
#include <stdio.h>

#include "svc_ai_agent/runtime.h"

enum {
    CALL_CLEAR,
    CALL_BEGIN,
    CALL_CAPTURE_START,
    CALL_CAPTURE_STOP,
    CALL_FORWARD,
    CALL_END,
    CALL_CANCEL,
    CALL_PLAYBACK_START,
    CALL_PLAYBACK_STOP,
};

typedef struct {
    unsigned calls[64];
    uint32_t turns[64];
    size_t count;
    bool wake;
    pet_ai_activity_t activity;
    bool vad_fail;
    bool forward_fail;
    bool capture_start_fail;
    bool capture_stop_fail;
    bool cancel_fail;
    bool clear_fail;
    size_t forwarded_samples;
} fake_ports_t;

static void record(fake_ports_t *ports, unsigned call)
{
    assert(ports->count < sizeof(ports->calls) / sizeof(ports->calls[0]));
    ports->calls[ports->count++] = call;
}

static void record_turn(fake_ports_t *ports, unsigned call, uint32_t turn)
{
    record(ports, call);
    ports->turns[ports->count - 1] = turn;
}

static pet_ai_port_result_t clear_audio(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_CLEAR, turn);
    return ports->clear_fail ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t begin_turn(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_BEGIN, turn); return PET_AI_PORT_OK;
}
static pet_ai_port_result_t start_capture(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_CAPTURE_START, turn);
    return ports->capture_start_fail ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t stop_capture(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_CAPTURE_STOP, turn);
    return ports->capture_stop_fail ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t forward_pcm(void *context, uint32_t turn,
                                        const int16_t *pcm, size_t samples)
{
    fake_ports_t *ports = context; assert(pcm != NULL); record_turn(ports, CALL_FORWARD, turn);
    ports->forwarded_samples += samples;
    return ports->forward_fail ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t end_input(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_END, turn); return PET_AI_PORT_OK;
}
static pet_ai_port_result_t cancel_turn(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_CANCEL, turn);
    return ports->cancel_fail ? PET_AI_PORT_FAILED : PET_AI_PORT_OK;
}
static pet_ai_port_result_t start_playback(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_PLAYBACK_START, turn); return PET_AI_PORT_OK;
}
static pet_ai_port_result_t stop_playback(void *context, uint32_t turn)
{
    fake_ports_t *ports = context; record_turn(ports, CALL_PLAYBACK_STOP, turn); return PET_AI_PORT_OK;
}
static pet_ai_port_result_t detect_wake(void *context, const int16_t *pcm, size_t samples,
                                        uint64_t now, bool *detected)
{
    fake_ports_t *ports = context;
    assert(pcm != NULL && samples == PET_AUDIO_CAPTURE_SAMPLES && now > 0 && detected != NULL);
    *detected = ports->wake;
    ports->wake = false;
    return PET_AI_PORT_OK;
}
static pet_ai_port_result_t classify_vad(void *context, const int16_t *pcm, size_t samples,
                                         uint64_t now, pet_ai_activity_t *activity)
{
    fake_ports_t *ports = context;
    assert(pcm != NULL && samples == PET_AUDIO_CAPTURE_SAMPLES && now > 0 && activity != NULL);
    if (ports->vad_fail) return PET_AI_PORT_FAILED;
    *activity = ports->activity;
    return PET_AI_PORT_OK;
}

static pet_ai_runtime_t make_runtime(fake_ports_t *ports)
{
    const pet_ai_runtime_sink_t sink = {
        .context = ports, .begin_turn = begin_turn, .start_capture = start_capture,
        .stop_capture = stop_capture, .forward_pcm = forward_pcm, .end_input = end_input,
        .cancel_turn = cancel_turn, .start_playback = start_playback,
        .stop_playback = stop_playback, .clear_audio = clear_audio,
    };
    const pet_ai_runtime_frontend_t frontend = {
        .context = ports, .detect_wake = detect_wake, .classify_vad = classify_vad,
    };
    pet_ai_runtime_t runtime;
    assert(pet_ai_runtime_init(&runtime, NULL, &sink, &frontend, 0) == PET_AI_RUNTIME_OK);
    return runtime;
}

static pet_ai_runtime_step_t frame(pet_ai_runtime_t *runtime, const int16_t *pcm, uint64_t now)
{
    pet_ai_runtime_step_t step;
    assert(pet_ai_runtime_frame(runtime, pcm, PET_AUDIO_CAPTURE_SAMPLES, now, &step) ==
           PET_AI_RUNTIME_OK);
    return step;
}

static pet_ai_runtime_step_t handle(pet_ai_runtime_t *runtime, pet_ai_input_kind_t kind,
                                    uint32_t turn, uint64_t now)
{
    pet_ai_runtime_step_t step;
    const pet_ai_input_t input = {.kind = kind, .turn_id = turn, .now_ms = now};
    assert(pet_ai_runtime_handle(runtime, &input, &step) == PET_AI_RUNTIME_OK);
    return step;
}

static void check_owner_flow_and_order(void)
{
    fake_ports_t ports = {.activity = PET_AI_ACTIVITY_SPEECH};
    pet_ai_runtime_t runtime = make_runtime(&ports);
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {1200};
    pet_ai_runtime_step_t step;

    assert(pet_ai_runtime_frame(&runtime, pcm, sizeof(pcm) / sizeof(pcm[0]), 20, &step) ==
           PET_AI_RUNTIME_IGNORED);
    assert(step.session_result == PET_AI_IGNORED && ports.count == 0);

    ports.wake = true;
    step = frame(&runtime, pcm, 40);
    assert(step.session_step.view.state == PET_AI_LISTENING && step.session_step.view.turn_id == 1);
    assert(ports.count == 3 && ports.calls[0] == CALL_CLEAR && ports.calls[1] == CALL_BEGIN &&
           ports.calls[2] == CALL_CAPTURE_START);

    for (uint64_t now = 60; now <= 340; now += 20) frame(&runtime, pcm, now);
    assert(ports.forwarded_samples == 15u * PET_AUDIO_CAPTURE_SAMPLES);
    ports.activity = PET_AI_ACTIVITY_SILENCE;
    for (uint64_t now = 360; now <= 640; now += 20) step = frame(&runtime, pcm, now);
    assert(step.session_step.view.state == PET_AI_THINKING);
    assert(ports.calls[ports.count - 2] == CALL_FORWARD && ports.calls[ports.count - 1] == CALL_END);

    step = handle(&runtime, PET_AI_RESPONSE_BEGIN, 1, 660);
    assert(step.session_step.view.state == PET_AI_SPEAKING && ports.calls[ports.count - 1] == CALL_PLAYBACK_START);
    step = handle(&runtime, PET_AI_PLAYBACK_DRAINED, 1, 680);
    assert(step.session_step.view.state == PET_AI_IDLE && ports.calls[ports.count - 2] == CALL_PLAYBACK_STOP &&
           ports.calls[ports.count - 1] == CALL_CLEAR);
}

static void check_stale_and_port_failures(void)
{
    fake_ports_t ports = {.activity = PET_AI_ACTIVITY_SPEECH};
    pet_ai_runtime_t runtime = make_runtime(&ports);
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {1200};
    ports.wake = true;
    frame(&runtime, pcm, 20);
    handle(&runtime, PET_AI_CANCEL, 0, 40);
    pet_ai_runtime_step_t step = handle(&runtime, PET_AI_TALK_PRESS, 0, 60);
    assert(step.session_step.view.turn_id == 2 && step.session_step.view.state == PET_AI_LISTENING);
    const size_t calls_before = ports.count;
    const pet_ai_input_t stale = {.kind = PET_AI_RESPONSE_BEGIN, .turn_id = 1, .now_ms = 70};
    assert(pet_ai_runtime_handle(&runtime, &stale, &step) == PET_AI_RUNTIME_STALE);
    assert(step.session_result == PET_AI_STALE && ports.count == calls_before);

    ports.forward_fail = true;
    assert(pet_ai_runtime_frame(&runtime, pcm, PET_AUDIO_CAPTURE_SAMPLES, 80, &step) ==
           PET_AI_RUNTIME_PORT_FAILED);
    assert(step.session_step.view.state == PET_AI_ERROR &&
           step.session_step.view.reason == PET_AI_REASON_TRANSPORT_ERROR &&
           step.port_result == PET_AI_PORT_FAILED);
    assert(step.cleanup_result == PET_AI_PORT_OK);
    assert(ports.count == calls_before + 4);
    assert(ports.calls[calls_before] == CALL_FORWARD);
    assert(ports.calls[calls_before + 1] == CALL_CANCEL);
    assert(ports.calls[calls_before + 2] == CALL_CAPTURE_STOP);
    assert(ports.calls[calls_before + 3] == CALL_CLEAR);

    ports.forward_fail = false;
    ports.capture_start_fail = true;
    const pet_ai_input_t press = {.kind = PET_AI_TALK_PRESS, .now_ms = 100};
    assert(pet_ai_runtime_handle(&runtime, &press, &step) == PET_AI_RUNTIME_PORT_FAILED);
    assert(step.session_step.view.state == PET_AI_ERROR &&
           step.session_step.view.reason == PET_AI_REASON_AUDIO_ERROR &&
           step.port_result == PET_AI_PORT_FAILED);

    ports.capture_start_fail = false;
    step = handle(&runtime, PET_AI_TALK_PRESS, 0, 120);
    assert(step.session_step.view.state == PET_AI_LISTENING);
    ports.vad_fail = true;
    assert(pet_ai_runtime_frame(&runtime, pcm, PET_AUDIO_CAPTURE_SAMPLES, 140, &step) ==
           PET_AI_RUNTIME_PORT_FAILED);
    assert(step.session_step.view.state == PET_AI_ERROR && step.port_result == PET_AI_PORT_FAILED);

    ports.vad_fail = false;
    const size_t before_old_frame = ports.count;
    assert(pet_ai_runtime_frame(&runtime, pcm, PET_AUDIO_CAPTURE_SAMPLES, 139, &step) ==
           PET_AI_RUNTIME_STALE);
    assert(step.session_result == PET_AI_STALE && ports.count == before_old_frame);
}

static void check_barge_in_targets_old_turn(void)
{
    fake_ports_t ports = {.activity = PET_AI_ACTIVITY_SPEECH};
    pet_ai_runtime_t runtime = make_runtime(&ports);
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {1200};
    ports.wake = true;
    frame(&runtime, pcm, 20);
    for (uint64_t now = 40; now <= 320; now += 20) frame(&runtime, pcm, now);
    ports.activity = PET_AI_ACTIVITY_SILENCE;
    for (uint64_t now = 340; now <= 620; now += 20) frame(&runtime, pcm, now);
    handle(&runtime, PET_AI_RESPONSE_BEGIN, 1, 640);
    const size_t start = ports.count;
    const pet_ai_runtime_step_t step = handle(&runtime, PET_AI_TALK_PRESS, 0, 660);
    assert(step.session_step.view.state == PET_AI_LISTENING && step.session_step.view.turn_id == 2);
    assert(ports.count == start + 5);
    assert(ports.calls[start] == CALL_CANCEL && ports.turns[start] == 1);
    assert(ports.calls[start + 1] == CALL_PLAYBACK_STOP && ports.turns[start + 1] == 1);
    assert(ports.calls[start + 2] == CALL_CLEAR && ports.turns[start + 2] == 1);
    assert(ports.calls[start + 3] == CALL_BEGIN && ports.turns[start + 3] == 2);
    assert(ports.calls[start + 4] == CALL_CAPTURE_START && ports.turns[start + 4] == 2);
}

static void check_arguments_and_optional_frontend(void)
{
    fake_ports_t ports = {0};
    const pet_ai_runtime_sink_t sink = {
        .context = &ports, .begin_turn = begin_turn, .start_capture = start_capture,
        .stop_capture = stop_capture, .forward_pcm = forward_pcm, .end_input = end_input,
        .cancel_turn = cancel_turn, .start_playback = start_playback,
        .stop_playback = stop_playback, .clear_audio = clear_audio,
    };
    pet_ai_runtime_t runtime;
    assert(pet_ai_runtime_init(NULL, NULL, &sink, NULL, 0) == PET_AI_RUNTIME_INVALID);
    assert(pet_ai_runtime_init(&runtime, NULL, NULL, NULL, 0) == PET_AI_RUNTIME_INVALID);
    assert(pet_ai_runtime_init(&runtime, NULL, &sink, NULL, 0) == PET_AI_RUNTIME_OK);
    pet_ai_runtime_step_t step;
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {900};
    assert(pet_ai_runtime_frame(&runtime, pcm, 319, 20, &step) == PET_AI_RUNTIME_INVALID);
    assert(pet_ai_runtime_frame(&runtime, NULL, PET_AUDIO_CAPTURE_SAMPLES, 20, &step) ==
           PET_AI_RUNTIME_INVALID);
    assert(!pet_ai_runtime_snapshot(NULL, NULL));
    assert(pet_ai_runtime_snapshot(&runtime, &step.session_step.view));
    assert(step.session_step.view.state == PET_AI_IDLE);
}

static void check_cleanup_failure_is_bounded(void)
{
    fake_ports_t ports = {.activity = PET_AI_ACTIVITY_SPEECH};
    pet_ai_runtime_t runtime = make_runtime(&ports);
    handle(&runtime, PET_AI_TALK_PRESS, 0, 20);
    const size_t before_cancel = ports.count;
    ports.capture_stop_fail = true;
    const pet_ai_input_t cancel = {.kind = PET_AI_CANCEL, .now_ms = 40};
    pet_ai_runtime_step_t step;
    assert(pet_ai_runtime_handle(&runtime, &cancel, &step) == PET_AI_RUNTIME_PORT_FAILED);
    assert(ports.count == before_cancel + 3);
    assert(ports.calls[before_cancel] == CALL_CANCEL);
    assert(ports.calls[before_cancel + 1] == CALL_CAPTURE_STOP);
    assert(ports.calls[before_cancel + 2] == CALL_CLEAR);
    assert(step.cleanup_result == PET_AI_PORT_FAILED && step.session_step.view.state == PET_AI_IDLE);

    ports.capture_stop_fail = false;
    handle(&runtime, PET_AI_TALK_PRESS, 0, 60);
    const size_t before_forward = ports.count;
    ports.forward_fail = ports.clear_fail = true;
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {1200};
    assert(pet_ai_runtime_frame(&runtime, pcm, PET_AUDIO_CAPTURE_SAMPLES, 80, &step) ==
           PET_AI_RUNTIME_PORT_FAILED);
    assert(step.session_step.view.state == PET_AI_ERROR && step.cleanup_result == PET_AI_PORT_FAILED);
    assert(ports.count == before_forward + 4);
    assert(ports.calls[before_forward + 3] == CALL_CLEAR);
}

static void check_failed_old_turn_blocks_new_turn(void)
{
    fake_ports_t ports = {.activity = PET_AI_ACTIVITY_SPEECH};
    pet_ai_runtime_t runtime = make_runtime(&ports);
    handle(&runtime, PET_AI_TALK_PRESS, 0, 20);
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {1200};
    for (uint64_t now = 40; now <= 320; now += 20) frame(&runtime, pcm, now);
    handle(&runtime, PET_AI_TALK_RELEASE, 1, 340);
    ports.cancel_fail = true;
    const size_t before = ports.count;
    const pet_ai_input_t press = {.kind = PET_AI_TALK_PRESS, .now_ms = 360};
    pet_ai_runtime_step_t step;
    assert(pet_ai_runtime_handle(&runtime, &press, &step) == PET_AI_RUNTIME_PORT_FAILED);
    assert(step.session_step.view.turn_id == 2 && step.session_step.view.state == PET_AI_ERROR);
    assert(step.cleanup_result == PET_AI_PORT_FAILED);
    assert(ports.count == before + 2);
    assert(ports.calls[before] == CALL_CANCEL && ports.turns[before] == 1);
    assert(ports.calls[before + 1] == CALL_CLEAR && ports.turns[before + 1] == 1);
}

int main(void)
{
    check_owner_flow_and_order();
    check_stale_and_port_failures();
    check_barge_in_targets_old_turn();
    check_arguments_and_optional_frontend();
    check_cleanup_failure_is_bounded();
    check_failed_old_turn_blocks_new_turn();
    puts("AI runtime: single-owner ordering, frontend injection, stale events, port failures and bounded cleanup passed");
}
