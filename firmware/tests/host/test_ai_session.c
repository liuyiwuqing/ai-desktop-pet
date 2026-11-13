#include <assert.h>
#include <stdio.h>

#include "svc_ai_agent/session.h"

static pet_ai_step_t command(pet_ai_session_t *session, pet_ai_input_kind_t kind,
                              uint32_t turn_id, uint64_t now)
{
    pet_ai_step_t step;
    const pet_ai_input_t input = {.kind = kind, .turn_id = turn_id, .now_ms = now};
    assert(pet_ai_session_handle(session, &input, &step) == PET_AI_OK);
    return step;
}

static pet_ai_step_t audio(pet_ai_session_t *session, uint32_t turn_id, uint64_t now,
                            int16_t amplitude)
{
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES];
    for (size_t i = 0; i < PET_AUDIO_CAPTURE_SAMPLES; ++i) pcm[i] = i % 2 ? amplitude : -amplitude;
    const pet_ai_audio_input_t input = {
        .turn_id = turn_id, .now_ms = now, .pcm = pcm,
        .samples = PET_AUDIO_CAPTURE_SAMPLES, .activity = PET_AI_ACTIVITY_ENERGY,
    };
    pet_ai_step_t step;
    assert(pet_ai_session_audio(session, &input, &step) == PET_AI_OK);
    return step;
}

static void check_voice_then_silence(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    pet_ai_view_t view;
    assert(pet_ai_session_snapshot(&session, &view) && view.state == PET_AI_IDLE && view.turn_id == 0);
    pet_ai_step_t step = command(&session, PET_AI_WAKE_DETECTED, 0, 0);
    assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 1);
    assert(step.actions == (PET_AI_ACTION_STATE_CHANGED | PET_AI_ACTION_BEGIN_TURN |
                            PET_AI_ACTION_START_CAPTURE | PET_AI_ACTION_CLEAR_AUDIO));
    for (uint64_t now = 20; now <= 300; now += 20) {
        step = audio(&session, 1, now, 1200);
        assert(step.view.state == PET_AI_LISTENING && step.actions == PET_AI_ACTION_FORWARD_PCM);
    }
    for (uint64_t now = 320; now <= 580; now += 20) {
        step = audio(&session, 1, now, 0);
        assert(step.view.state == PET_AI_LISTENING);
    }
    step = audio(&session, 1, 600, 0);
    assert(step.view.state == PET_AI_THINKING && step.view.reason == PET_AI_REASON_SILENCE);
    assert(step.view.speech_ms == 300 && step.view.silence_ms == 300 && step.view.level_pct == 0);
    assert(step.actions == (PET_AI_ACTION_FORWARD_PCM | PET_AI_ACTION_STOP_CAPTURE |
                            PET_AI_ACTION_END_INPUT | PET_AI_ACTION_STATE_CHANGED));
    step = command(&session, PET_AI_RESPONSE_BEGIN, 1, 700);
    assert(step.view.state == PET_AI_SPEAKING && step.actions ==
        (PET_AI_ACTION_START_PLAYBACK | PET_AI_ACTION_STATE_CHANGED));
    step = command(&session, PET_AI_PLAYBACK_DRAINED, 1, 900);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_DRAINED);
    assert(step.actions == (PET_AI_ACTION_STOP_PLAYBACK | PET_AI_ACTION_CLEAR_AUDIO |
                            PET_AI_ACTION_STATE_CHANGED));
}

static void check_noise_and_listen_limit(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    command(&session, PET_AI_WAKE_DETECTED, 0, 0);
    pet_ai_step_t step;
    for (uint64_t now = 20; now <= 280; now += 20) audio(&session, 1, now, 1200);
    for (uint64_t now = 300; now <= 580; now += 20) step = audio(&session, 1, now, 0);
    assert(step.view.state == PET_AI_LISTENING && step.view.speech_ms == 0);
    /* 已经结束的短噪声不能与之后的单帧噪声凑足 300 ms。 */
    audio(&session, 1, 600, 1200);
    for (uint64_t now = 620; now <= 900; now += 20) step = audio(&session, 1, now, 0);
    assert(step.view.state == PET_AI_LISTENING && step.view.speech_ms == 0);
    for (uint64_t now = 920; now <= 15000; now += 20) step = audio(&session, 1, now, 0);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_NO_SPEECH);
    assert((step.actions & (PET_AI_ACTION_CANCEL_TURN | PET_AI_ACTION_STOP_CAPTURE |
                            PET_AI_ACTION_CLEAR_AUDIO)) ==
        (PET_AI_ACTION_CANCEL_TURN | PET_AI_ACTION_STOP_CAPTURE | PET_AI_ACTION_CLEAR_AUDIO));
    assert(!(step.actions & (PET_AI_ACTION_END_INPUT | PET_AI_ACTION_FORWARD_PCM)));
    assert(step.cancelled_turn_id == 1);

    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    command(&session, PET_AI_WAKE_DETECTED, 0, 0);
    for (uint64_t now = 20; now <= 15000; now += 20) {
        step = audio(&session, 1, now, 1200);
        if (now < 15000) assert(step.view.state == PET_AI_LISTENING);
    }
    assert(step.view.state == PET_AI_THINKING && step.view.reason == PET_AI_REASON_LIMIT);
    assert(step.view.speech_ms == 15000 && (step.actions & PET_AI_ACTION_END_INPUT));
}

static void check_push_to_talk_and_barge_in(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    pet_ai_step_t step = command(&session, PET_AI_TALK_PRESS, 0, 0);
    assert(step.view.push_to_talk && step.view.state == PET_AI_LISTENING);
    for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
    for (uint64_t now = 320; now <= 600; now += 20) step = audio(&session, 1, now, 0);
    assert(step.view.state == PET_AI_LISTENING);
    step = command(&session, PET_AI_TALK_RELEASE, 1, 610);
    assert(step.view.state == PET_AI_THINKING && step.view.reason == PET_AI_REASON_RELEASE);
    assert(step.actions & PET_AI_ACTION_END_INPUT);
    command(&session, PET_AI_RESPONSE_BEGIN, 1, 700);
    const pet_ai_input_t wake = {.kind = PET_AI_WAKE_DETECTED, .now_ms = 710};
    assert(pet_ai_session_handle(&session, &wake, &step) == PET_AI_IGNORED);
    assert(step.view.state == PET_AI_SPEAKING && step.actions == 0); /* 无 AEC 时只接受手动打断。 */
    step = command(&session, PET_AI_TALK_PRESS, 0, 720);
    assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 2);
    assert(step.cancelled_turn_id == 1 && step.view.speech_ms == 0);
    assert(step.actions == (PET_AI_ACTION_CANCEL_TURN | PET_AI_ACTION_STOP_PLAYBACK |
                            PET_AI_ACTION_CLEAR_AUDIO | PET_AI_ACTION_BEGIN_TURN |
                            PET_AI_ACTION_START_CAPTURE | PET_AI_ACTION_STATE_CHANGED));
    audio(&session, 2, 740, 1200);
    step = command(&session, PET_AI_TALK_RELEASE, 2, 750);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_NO_SPEECH);
    assert((step.actions & PET_AI_ACTION_CANCEL_TURN) && !(step.actions & PET_AI_ACTION_END_INPUT));
}

static void check_mute_cancel_and_failures(void)
{
    for (int phase = 0; phase < 3; ++phase) {
        pet_ai_session_t session;
        pet_ai_step_t step;
        assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
        command(&session, PET_AI_TALK_PRESS, 0, 0);
        for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
        if (phase > 0) command(&session, PET_AI_TALK_RELEASE, 1, 310);
        if (phase > 1) command(&session, PET_AI_RESPONSE_BEGIN, 1, 320);
        step = command(&session, PET_AI_MUTE, 0, 400);
        assert(step.view.state == PET_AI_MUTED && step.view.level_pct == 0);
        assert(step.cancelled_turn_id == 1 && (step.actions & PET_AI_ACTION_CLEAR_AUDIO));
        assert(((step.actions & PET_AI_ACTION_STOP_CAPTURE) != 0) == (phase == 0));
        assert(((step.actions & PET_AI_ACTION_STOP_PLAYBACK) != 0) == (phase == 2));
        for (pet_ai_input_kind_t kind = PET_AI_WAKE_DETECTED; kind <= PET_AI_RESPONSE_BEGIN; ++kind) {
            const pet_ai_input_t input = {
                .kind = kind, .turn_id = kind >= PET_AI_TALK_RELEASE ? 1 : 0, .now_ms = 410,
            };
            assert(pet_ai_session_handle(&session, &input, &step) == PET_AI_IGNORED);
            assert(step.actions == 0 && step.view.state == PET_AI_MUTED);
        }
        step = command(&session, PET_AI_UNMUTE, 0, 420);
        assert(step.view.state == PET_AI_IDLE && !(step.actions & PET_AI_ACTION_START_CAPTURE));
        step = command(&session, PET_AI_TALK_PRESS, 0, 430);
        assert(step.view.turn_id == 2 && step.view.speech_ms == 0);
        step = command(&session, PET_AI_CANCEL, 0, 440);
        assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_CANCELLED);
        assert(step.cancelled_turn_id == 2 && (step.actions & PET_AI_ACTION_STOP_CAPTURE));
        command(&session, PET_AI_TALK_PRESS, 0, 450);
        step = command(&session, PET_AI_AUDIO_FAILED, 3, 460);
        assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_AUDIO_ERROR);
        assert(step.cancelled_turn_id == 3 && (step.actions & PET_AI_ACTION_STOP_CAPTURE));
        step = command(&session, PET_AI_TALK_PRESS, 0, 470);
        assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 4);
        step = command(&session, PET_AI_TRANSPORT_FAILED, 4, 480);
        assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_TRANSPORT_ERROR);
        step = command(&session, PET_AI_CANCEL, 0, 490);
        assert(step.view.state == PET_AI_IDLE && step.cancelled_turn_id == 0);
    }
}

static void assert_same_view(pet_ai_view_t a, pet_ai_view_t b)
{
    assert(a.turn_id == b.turn_id && a.state == b.state && a.reason == b.reason &&
           a.speech_ms == b.speech_ms && a.silence_ms == b.silence_ms &&
           a.level_pct == b.level_pct && a.push_to_talk == b.push_to_talk);
}

static void check_old_turns_duplicates_and_invalid_input(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    command(&session, PET_AI_TALK_PRESS, 0, 0);
    for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 1, 310);
    command(&session, PET_AI_RESPONSE_BEGIN, 1, 320);
    pet_ai_step_t step = command(&session, PET_AI_TALK_PRESS, 0, 330);
    pet_ai_view_t before = step.view;
    const pet_ai_input_kind_t scoped[] = {PET_AI_TALK_RELEASE, PET_AI_RESPONSE_BEGIN,
        PET_AI_PLAYBACK_DRAINED, PET_AI_AUDIO_FAILED, PET_AI_TRANSPORT_FAILED};
    for (size_t i = 0; i < sizeof(scoped) / sizeof(scoped[0]); ++i) {
        const pet_ai_input_t input = {.kind = scoped[i], .turn_id = 1, .now_ms = UINT64_MAX};
        assert(pet_ai_session_handle(&session, &input, &step) == PET_AI_STALE);
        assert(step.actions == 0 && step.cancelled_turn_id == 0);
        assert_same_view(before, step.view);
    }
    int16_t pcm[320] = {0};
    pet_ai_audio_input_t frame = {.turn_id = 1, .now_ms = UINT64_MAX, .pcm = pcm, .samples = 320};
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_STALE && step.actions == 0);
    audio(&session, 2, 350, 1200);
    pet_ai_session_snapshot(&session, &before);
    frame.turn_id = 2;
    for (uint64_t now = 349; now <= 369; ++now) {
        frame.now_ms = now;
        assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_STALE);
        assert(step.actions == 0);
        assert_same_view(before, step.view);
    }
    frame.now_ms = 370; frame.samples = 319;
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_INVALID);
    frame.samples = 320; frame.activity = (pet_ai_activity_t)99;
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_INVALID);
    assert_same_view(before, step.view);
    audio(&session, 2, 370, 1200);
    pet_ai_input_t invalid = {.kind = PET_AI_MUTE, .turn_id = 2, .now_ms = 380};
    assert(pet_ai_session_handle(&session, &invalid, &step) == PET_AI_INVALID);
    invalid.kind = PET_AI_TALK_RELEASE; invalid.turn_id = 0;
    assert(pet_ai_session_handle(&session, &invalid, &step) == PET_AI_INVALID);
    invalid.kind = PET_AI_MUTE; invalid.now_ms = 369;
    assert(pet_ai_session_handle(&session, &invalid, &step) == PET_AI_STALE);
    command(&session, PET_AI_MUTE, 0, 380);
    frame.now_ms = 400; frame.activity = PET_AI_ACTIVITY_ENERGY;
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_IGNORED && step.actions == 0);
    assert(step.view.state == PET_AI_MUTED);
}

static void check_deadlines_gaps_and_wake_cooldown(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    command(&session, PET_AI_WAKE_DETECTED, 0, 0);
    command(&session, PET_AI_CANCEL, 0, 20);
    pet_ai_step_t step;
    for (uint64_t now = 999; now <= 2999; now += 1000) {
        const pet_ai_input_t wake = {.kind = PET_AI_WAKE_DETECTED, .now_ms = now};
        assert(pet_ai_session_handle(&session, &wake, &step) == PET_AI_IGNORED && step.actions == 0);
    }
    step = command(&session, PET_AI_WAKE_DETECTED, 0, 3000);
    assert(step.view.turn_id == 2);
    step = command(&session, PET_AI_TICK, 0, 3100);
    assert(step.view.state == PET_AI_LISTENING && step.actions == 0);
    step = command(&session, PET_AI_TICK, 0, 3101);
    assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_INPUT_GAP);
    assert(step.cancelled_turn_id == 2 && (step.actions & PET_AI_ACTION_STOP_CAPTURE));
    assert(!(step.actions & PET_AI_ACTION_END_INPUT));

    command(&session, PET_AI_TALK_PRESS, 0, 3200);
    step = audio(&session, 3, 3301, 1200);
    assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_INPUT_GAP);
    assert(!(step.actions & PET_AI_ACTION_FORWARD_PCM));

    for (int at_deadline = 0; at_deadline < 2; ++at_deadline) {
        assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
        command(&session, PET_AI_TALK_PRESS, 0, 0);
        for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
        command(&session, PET_AI_TALK_RELEASE, 1, 310);
        step = command(&session, PET_AI_TICK, 0, 15309);
        assert(step.view.state == PET_AI_THINKING && step.actions == 0);
        step = command(&session, at_deadline ? PET_AI_RESPONSE_BEGIN : PET_AI_TICK,
                       at_deadline ? 1 : 0, 15310);
        assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_RESPONSE_TIMEOUT);
        assert(step.cancelled_turn_id == 1 && !(step.actions & PET_AI_ACTION_START_PLAYBACK));
    }
    /* 使用 64-bit 单调时钟，跨越 32-bit 毫秒边界不丢帧、不提前超时。 */
    const uint64_t start = UINT32_MAX - UINT64_C(100);
    assert(pet_ai_session_init(&session, NULL, start) == PET_AI_OK);
    command(&session, PET_AI_WAKE_DETECTED, 0, start);
    for (uint64_t offset = 20; offset <= 600; offset += 20)
        step = audio(&session, 1, start + offset, offset <= 300 ? 1200 : 0);
    assert(step.view.state == PET_AI_THINKING && step.view.reason == PET_AI_REASON_SILENCE);
}

static void check_activity_labels_hysteresis_and_arguments(void)
{
    pet_ai_session_t session = {0};
    pet_ai_step_t step;
    pet_ai_input_t input = {.kind = PET_AI_TALK_PRESS};
    assert(pet_ai_session_handle(&session, &input, &step) == PET_AI_NOT_READY);
    assert(!pet_ai_session_snapshot(&session, &step.view));
    assert(pet_ai_session_init(NULL, NULL, 0) == PET_AI_INVALID);
    pet_ai_session_config_t config = {.energy_on_rms = 800, .energy_off_rms = 800};
    assert(pet_ai_session_init(&session, &config, 0) == PET_AI_INVALID);
    config.energy_on_rms = 32769; config.energy_off_rms = 400;
    assert(pet_ai_session_init(&session, &config, 0) == PET_AI_INVALID);
    assert(pet_ai_session_init(&session, NULL, 0) == PET_AI_OK);
    assert(pet_ai_session_handle(&session, NULL, &step) == PET_AI_INVALID);
    assert(pet_ai_session_handle(&session, &input, NULL) == PET_AI_INVALID);
    command(&session, PET_AI_TALK_PRESS, 0, 0);
    step = audio(&session, 1, 20, 600);
    assert(step.view.speech_ms == 0);
    step = audio(&session, 1, 40, 800);
    assert(step.view.speech_ms == 20);
    step = audio(&session, 1, 60, 600);
    assert(step.view.speech_ms == 40);
    step = audio(&session, 1, 80, 400);
    assert(step.view.speech_ms == 40 && step.view.silence_ms == 20);
    step = audio(&session, 1, 100, 600);
    assert(step.view.speech_ms == 40 && step.view.silence_ms == 40);

    int16_t pcm[320] = {0};
    pet_ai_audio_input_t frame = {.turn_id = 1, .now_ms = 120, .pcm = pcm,
                                 .samples = 320, .activity = PET_AI_ACTIVITY_SPEECH};
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_OK);
    assert(step.view.speech_ms == 60 && step.view.silence_ms == 0 && step.view.level_pct == 0);
    step = audio(&session, 1, 140, 600);
    assert(step.view.speech_ms == 80); /* 明确的模型 VAD 结果也更新回退滞回状态。 */
    for (unsigned i = 0; i < 320; ++i) pcm[i] = 5000;
    frame.now_ms = 160; frame.activity = PET_AI_ACTIVITY_SILENCE;
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_OK);
    assert(step.view.speech_ms == 80 && step.view.silence_ms == 20 && step.view.level_pct > 0);
    step = audio(&session, 1, 180, 600);
    assert(step.view.speech_ms == 80 && step.view.silence_ms == 40);
    assert(pet_ai_session_audio(&session, NULL, &step) == PET_AI_INVALID);
    frame.pcm = NULL; frame.now_ms = 200;
    assert(pet_ai_session_audio(&session, &frame, &step) == PET_AI_INVALID);
    pet_ai_view_t before;
    pet_ai_session_snapshot(&session, &before);
    config.energy_on_rms = 0;
    assert(pet_ai_session_init(&session, &config, 0) == PET_AI_INVALID);
    pet_ai_view_t after;
    pet_ai_session_snapshot(&session, &after);
    assert_same_view(before, after);
}

int main(void)
{
    check_voice_then_silence();
    check_noise_and_listen_limit();
    check_push_to_talk_and_barge_in();
    check_mute_cancel_and_failures();
    check_old_turns_duplicates_and_invalid_input();
    check_deadlines_gaps_and_wake_cooldown();
    check_activity_labels_hysteresis_and_arguments();
    puts("AI session: endpoints, controls, correlation, deadlines, VAD and input boundaries passed");
}
