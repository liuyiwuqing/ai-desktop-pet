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

static void check_conversation_window(void)
{
    pet_ai_session_t session;
    assert(pet_ai_session_init(&session, &(pet_ai_session_config_t){.energy_on_rms = 800,
        .energy_off_rms = 400, .conversation_window_ms = 1000}, 0) == PET_AI_INVALID);
    const pet_ai_session_config_t config = {.energy_on_rms = 800, .energy_off_rms = 400,
        .conversation_window_ms = 1000, .conversation_max_rounds = 2};
    assert(pet_ai_session_init(&session, &config, 0) == PET_AI_OK);

    command(&session, PET_AI_TALK_PRESS, 0, 0);
    for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 1, 310);
    command(&session, PET_AI_RESPONSE_BEGIN, 1, 400);
    pet_ai_step_t step = command(&session, PET_AI_PLAYBACK_DRAINED, 1, 500);
    assert(step.view.state == PET_AI_CONVERSATION && step.view.reason == PET_AI_REASON_DRAINED);
    assert(step.actions == (PET_AI_ACTION_STOP_PLAYBACK | PET_AI_ACTION_CLEAR_AUDIO |
                            PET_AI_ACTION_START_CAPTURE | PET_AI_ACTION_STATE_CHANGED));
    /* 窗口内只有 RMS 监听：不转发 PCM；短噪声不足 200 ms 不续轮。 */
    step = command(&session, PET_AI_TICK, 0, 520);
    assert(step.view.state == PET_AI_CONVERSATION && step.actions == 0);
    step = audio(&session, 1, 540, 1200);
    assert(step.view.state == PET_AI_CONVERSATION && step.actions == 0);
    assert(step.view.speech_ms == 20 && step.view.level_pct > 0);
    for (uint64_t now = 560; now <= 840; now += 20) step = audio(&session, 1, now, 0);
    assert(step.view.speech_ms == 0); /* 不足 MIN_SPEECH 的噪声在长静音后清零。 */
    for (uint64_t now = 860; now <= 1040; now += 20) step = audio(&session, 1, now, 1200);
    assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 2 &&
        step.view.reason == PET_AI_REASON_CONV_SPEECH);
    assert(step.actions == (PET_AI_ACTION_BEGIN_TURN | PET_AI_ACTION_STATE_CHANGED));
    /* 续轮复用已开启的采集；语音正常转发并按静音端点结束。 */
    for (uint64_t now = 1060; now <= 1600; now += 20) step = audio(&session, 2, now, 1200);
    assert(step.actions == PET_AI_ACTION_FORWARD_PCM);
    for (uint64_t now = 1620; now <= 1900; now += 20) step = audio(&session, 2, now, 0);
    assert(step.view.state == PET_AI_THINKING && (step.actions & PET_AI_ACTION_END_INPUT));
    command(&session, PET_AI_RESPONSE_BEGIN, 2, 2000);
    step = command(&session, PET_AI_PLAYBACK_DRAINED, 2, 2100);
    assert(step.view.state == PET_AI_CONVERSATION); /* 第二轮仍在窗口。 */
    /* 第二次续轮后轮次上限到达：drain 回到 IDLE，不再进入窗口。 */
    for (uint64_t now = 2120; now <= 2300; now += 20) audio(&session, 2, now, 1200);
    for (uint64_t now = 2320; now <= 2700; now += 20) step = audio(&session, 3, now, 1200);
    assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 3);
    for (uint64_t now = 2720; now <= 3000; now += 20) step = audio(&session, 3, now, 0);
    assert(step.view.state == PET_AI_THINKING);
    command(&session, PET_AI_RESPONSE_BEGIN, 3, 3100);
    step = command(&session, PET_AI_PLAYBACK_DRAINED, 3, 3200);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_DRAINED);
    assert(step.actions == (PET_AI_ACTION_STOP_PLAYBACK | PET_AI_ACTION_CLEAR_AUDIO |
                            PET_AI_ACTION_STATE_CHANGED));
}

static void check_conversation_exits(void)
{
    pet_ai_session_t session;
    const pet_ai_session_config_t config = {.energy_on_rms = 800, .energy_off_rms = 400,
        .conversation_window_ms = 500, .conversation_max_rounds = 10};
    assert(pet_ai_session_init(&session, &config, 0) == PET_AI_OK);
    command(&session, PET_AI_TALK_PRESS, 0, 0);
    for (uint64_t now = 20; now <= 300; now += 20) audio(&session, 1, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 1, 310);
    command(&session, PET_AI_RESPONSE_BEGIN, 1, 400);
    command(&session, PET_AI_PLAYBACK_DRAINED, 1, 500);
    /* 窗口超时回待机：停采集、清音频、会话记账归零。采集仍在跑时帧会持续刷新锚点。 */
    for (uint64_t now = 520; now <= 1000; now += 20) audio(&session, 1, now, 0);
    pet_ai_step_t step = command(&session, PET_AI_TICK, 0, 1010);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_CONV_TIMEOUT);
    assert(step.actions == (PET_AI_ACTION_STOP_CAPTURE | PET_AI_ACTION_CLEAR_AUDIO |
                            PET_AI_ACTION_STATE_CHANGED));

    /* 超时后再次 drain 属于新会话：重新开窗。 */
    command(&session, PET_AI_TALK_PRESS, 0, 1200);
    for (uint64_t now = 1220; now <= 1500; now += 20) audio(&session, 2, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 2, 1510);
    command(&session, PET_AI_RESPONSE_BEGIN, 2, 1600);
    command(&session, PET_AI_PLAYBACK_DRAINED, 2, 1700);
    pet_ai_view_t view;
    assert(pet_ai_session_snapshot(&session, &view) && view.state == PET_AI_CONVERSATION);
    /* 按住说话续轮：不重复启停采集。 */
    step = command(&session, PET_AI_TALK_PRESS, 0, 1750);
    assert(step.view.state == PET_AI_LISTENING && step.view.turn_id == 3 && step.view.push_to_talk);
    assert(step.actions == (PET_AI_ACTION_BEGIN_TURN | PET_AI_ACTION_START_CAPTURE |
                            PET_AI_ACTION_CLEAR_AUDIO | PET_AI_ACTION_STATE_CHANGED));
    /* 窗口内取消与静音都要真正停掉采集。 */
    step = command(&session, PET_AI_CANCEL, 0, 1800);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_CANCELLED);
    assert((step.actions & (PET_AI_ACTION_STOP_CAPTURE | PET_AI_ACTION_CLEAR_AUDIO)) ==
        (PET_AI_ACTION_STOP_CAPTURE | PET_AI_ACTION_CLEAR_AUDIO));
    command(&session, PET_AI_TALK_PRESS, 0, 1850);
    for (uint64_t now = 1870; now <= 2150; now += 20) audio(&session, 4, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 4, 2160);
    command(&session, PET_AI_RESPONSE_BEGIN, 4, 2200);
    command(&session, PET_AI_PLAYBACK_DRAINED, 4, 2300);
    assert(pet_ai_session_snapshot(&session, &view) && view.state == PET_AI_CONVERSATION);
    step = command(&session, PET_AI_MUTE, 0, 2350);
    assert(step.view.state == PET_AI_MUTED);
    assert(step.actions & PET_AI_ACTION_STOP_CAPTURE);
    /* 静音后不开麦，窗口帧被忽略；恢复也回待机而不是窗口。 */
    int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES] = {0};
    const pet_ai_audio_input_t frame = {.turn_id = 4, .now_ms = 2370, .pcm = pcm,
                                        .samples = PET_AUDIO_CAPTURE_SAMPLES};
    assert(pet_ai_session_audio(&session, &frame, &(pet_ai_step_t){0}) == PET_AI_IGNORED);
    step = command(&session, PET_AI_UNMUTE, 0, 2400);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_UNMUTED);
    command(&session, PET_AI_TALK_PRESS, 0, 2450);
    for (uint64_t now = 2470; now <= 2750; now += 20) audio(&session, 5, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 5, 2760);
    command(&session, PET_AI_RESPONSE_BEGIN, 5, 2800);
    command(&session, PET_AI_PLAYBACK_DRAINED, 5, 2900);
    assert(pet_ai_session_snapshot(&session, &view) && view.state == PET_AI_CONVERSATION);
    /* 会话总时长达到上限后 drain 回到 IDLE，不再续窗（episode 自 2900 ms 起）。 */
    command(&session, PET_AI_TALK_PRESS, 0, 301000);
    for (uint64_t now = 301020; now <= 301300; now += 20) audio(&session, 6, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 6, 301310);
    command(&session, PET_AI_RESPONSE_BEGIN, 6, 301400);
    step = command(&session, PET_AI_PLAYBACK_DRAINED, 6, 302910);
    assert(step.view.state == PET_AI_IDLE && step.view.reason == PET_AI_REASON_DRAINED);
    /* 音频/传输失败也退出会话并锁为错误，不是静默续窗。 */
    command(&session, PET_AI_TALK_PRESS, 0, 303000);
    for (uint64_t now = 303020; now <= 303300; now += 20) audio(&session, 7, now, 1200);
    command(&session, PET_AI_TALK_RELEASE, 7, 303310);
    command(&session, PET_AI_RESPONSE_BEGIN, 7, 303400);
    step = command(&session, PET_AI_PLAYBACK_DRAINED, 7, 303500);
    assert(step.view.state == PET_AI_CONVERSATION);
    step = command(&session, PET_AI_TRANSPORT_FAILED, 7, 303600);
    assert(step.view.state == PET_AI_ERROR && step.view.reason == PET_AI_REASON_TRANSPORT_ERROR);
    assert(step.actions & PET_AI_ACTION_STOP_CAPTURE);
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
    check_conversation_window();
    check_conversation_exits();
    puts("AI session: endpoints, controls, correlation, deadlines, VAD and input boundaries passed");
}
