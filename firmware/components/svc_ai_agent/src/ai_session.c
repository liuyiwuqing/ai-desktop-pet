#include "svc_ai_agent/session.h"

#include <string.h>

#include "pet/hal/audio_pcm.h"

pet_ai_result_t pet_ai_session_init(pet_ai_session_t *session,
                                   const pet_ai_session_config_t *config, uint64_t now_ms)
{
    const pet_ai_session_config_t defaults = {.energy_on_rms = 800, .energy_off_rms = 400};
    if (session == NULL) return PET_AI_INVALID;
    const pet_ai_session_config_t selected = config == NULL ? defaults : *config;
    if (selected.energy_on_rms == 0 || selected.energy_on_rms > 32768 ||
        selected.energy_off_rms >= selected.energy_on_rms) return PET_AI_INVALID;
    if (selected.conversation_window_ms != 0 && selected.conversation_max_rounds == 0)
        return PET_AI_INVALID;
    *session = (pet_ai_session_t){.config = selected, .last_now_ms = now_ms, .ready = true};
    return PET_AI_OK;
}

bool pet_ai_session_snapshot(const pet_ai_session_t *session, pet_ai_view_t *view)
{
    if (session == NULL || view == NULL || !session->ready) return false;
    *view = session->view;
    return true;
}

static pet_ai_result_t prepare(const pet_ai_session_t *session, pet_ai_step_t *step)
{
    if (step == NULL) return PET_AI_INVALID;
    *step = (pet_ai_step_t){0};
    return pet_ai_session_snapshot(session, &step->view) ? PET_AI_OK : PET_AI_NOT_READY;
}

static void state(pet_ai_session_t *session, pet_ai_state_t value, pet_ai_reason_t reason,
                   uint64_t now, pet_ai_step_t *step)
{
    if (session->view.state != value) step->actions |= PET_AI_ACTION_STATE_CHANGED;
    session->view.state = value;
    session->view.reason = reason;
    session->state_start_ms = now;
    if (value != PET_AI_LISTENING) session->view.level_pct = 0;
    step->view = session->view;
}

static void finish_input(pet_ai_session_t *session, pet_ai_reason_t reason,
                          uint64_t now, pet_ai_step_t *step)
{
    step->actions |= PET_AI_ACTION_STOP_CAPTURE;
    if (session->view.speech_ms >= PET_AI_MIN_SPEECH_MS) {
        step->actions |= PET_AI_ACTION_END_INPUT;
        state(session, PET_AI_THINKING, reason, now, step);
    } else {
        step->actions &= ~PET_AI_ACTION_FORWARD_PCM;
        step->actions |= PET_AI_ACTION_CANCEL_TURN | PET_AI_ACTION_CLEAR_AUDIO;
        step->cancelled_turn_id = session->view.turn_id;
        state(session, PET_AI_IDLE, PET_AI_REASON_NO_SPEECH, now, step);
    }
}

static void cancel_turn(const pet_ai_session_t *session, pet_ai_step_t *step)
{
    if (session->view.state == PET_AI_LISTENING || session->view.state == PET_AI_CONVERSATION)
        step->actions |= PET_AI_ACTION_STOP_CAPTURE;
    if (session->view.state == PET_AI_SPEAKING) step->actions |= PET_AI_ACTION_STOP_PLAYBACK;
    if (session->view.state == PET_AI_LISTENING || session->view.state == PET_AI_THINKING ||
        session->view.state == PET_AI_SPEAKING) {
        step->actions |= PET_AI_ACTION_CANCEL_TURN;
        step->cancelled_turn_id = session->view.turn_id;
    }
    step->actions |= PET_AI_ACTION_CLEAR_AUDIO;
}

/* 会话保持的轮次与总时长记账；退出会话（超时/取消/静音/失败/上限）必须调用 conv_end。 */
static void conv_end(pet_ai_session_t *session)
{
    session->conv_active = false;
    session->conv_rounds = 0;
}

static bool conv_open(pet_ai_session_t *session, uint64_t now)
{
    if (session->config.conversation_window_ms == 0) return false;
    if (session->conv_active) {
        if (now - session->conv_start_ms >= PET_AI_CONV_TOTAL_MS ||
            session->conv_rounds >= session->config.conversation_max_rounds) {
            conv_end(session);
            return false;
        }
    } else {
        session->conv_start_ms = now;
        session->conv_active = true;
        session->conv_rounds = 0;
    }
    ++session->conv_rounds;
    return true;
}

static bool turn_scoped(pet_ai_input_kind_t kind)
{
    return kind == PET_AI_TALK_RELEASE || kind == PET_AI_RESPONSE_BEGIN ||
           kind == PET_AI_PLAYBACK_DRAINED || kind == PET_AI_AUDIO_FAILED ||
           kind == PET_AI_TRANSPORT_FAILED;
}

static bool expire(pet_ai_session_t *session, uint64_t now, pet_ai_step_t *step)
{
    if (session->view.state == PET_AI_LISTENING) {
        if (now - session->last_frame_ms > PET_AI_FRAME_GAP_MS) {
            cancel_turn(session, step);
            state(session, PET_AI_ERROR, PET_AI_REASON_INPUT_GAP, now, step);
            return true;
        }
        if (now - session->listen_start_ms >= PET_AI_MAX_LISTEN_MS) {
            finish_input(session, PET_AI_REASON_LIMIT, now, step);
            return true;
        }
    } else if (session->view.state == PET_AI_CONVERSATION) {
        if (now - session->last_frame_ms > PET_AI_FRAME_GAP_MS) {
            cancel_turn(session, step);
            conv_end(session);
            state(session, PET_AI_ERROR, PET_AI_REASON_INPUT_GAP, now, step);
            return true;
        }
        if (now - session->state_start_ms >= session->config.conversation_window_ms) {
            cancel_turn(session, step);
            conv_end(session);
            state(session, PET_AI_IDLE, PET_AI_REASON_CONV_TIMEOUT, now, step);
            return true;
        }
    } else if (session->view.state == PET_AI_THINKING &&
               now - session->state_start_ms >= PET_AI_RESPONSE_TIMEOUT_MS) {
        cancel_turn(session, step);
        state(session, PET_AI_ERROR, PET_AI_REASON_RESPONSE_TIMEOUT, now, step);
        return true;
    }
    return false;
}

pet_ai_result_t pet_ai_session_handle(pet_ai_session_t *session,
                                     const pet_ai_input_t *input, pet_ai_step_t *step)
{
    const pet_ai_result_t ready = prepare(session, step);
    if (ready != PET_AI_OK) return ready;
    if (input == NULL || input->kind < PET_AI_WAKE_DETECTED || input->kind >= PET_AI_INPUT_COUNT)
        return PET_AI_INVALID;
    if (turn_scoped(input->kind)) {
        if (input->turn_id == 0) return PET_AI_INVALID;
        if (input->turn_id != session->view.turn_id) return PET_AI_STALE;
    } else if (input->turn_id != 0) {
        return PET_AI_INVALID;
    }
    if (input->now_ms < session->last_now_ms) return PET_AI_STALE;
    switch (input->kind) {
    case PET_AI_WAKE_DETECTED:
    case PET_AI_TALK_PRESS:
        if (session->view.state == PET_AI_MUTED || session->view.state == PET_AI_LISTENING)
            return PET_AI_IGNORED;
        if (input->kind == PET_AI_WAKE_DETECTED && session->view.state != PET_AI_IDLE &&
            session->view.state != PET_AI_ERROR) return PET_AI_IGNORED;
        if (input->kind == PET_AI_WAKE_DETECTED && session->have_wake &&
            input->now_ms - session->last_wake_ms < PET_AI_WAKE_COOLDOWN_MS) return PET_AI_IGNORED;
        if (session->view.turn_id == UINT32_MAX) return PET_AI_ID_EXHAUSTED;
        if (input->kind == PET_AI_WAKE_DETECTED) {
            session->have_wake = true;
            session->last_wake_ms = input->now_ms;
        }
        cancel_turn(session, step);
        if (session->view.state == PET_AI_CONVERSATION)
            step->actions &= ~PET_AI_ACTION_STOP_CAPTURE; /* 窗口内采集已开启，续轮不重启采集。 */
        ++session->view.turn_id;
        session->view.speech_ms = session->view.silence_ms = 0;
        session->view.push_to_talk = input->kind == PET_AI_TALK_PRESS;
        session->listen_start_ms = session->last_frame_ms = input->now_ms;
        session->energy_speech = false;
        step->actions |= PET_AI_ACTION_BEGIN_TURN | PET_AI_ACTION_START_CAPTURE;
        state(session, PET_AI_LISTENING,
              session->view.push_to_talk ? PET_AI_REASON_PRESS : PET_AI_REASON_WAKE, input->now_ms, step);
        break;
    case PET_AI_TALK_RELEASE:
        if (session->view.state != PET_AI_LISTENING || !session->view.push_to_talk)
            return PET_AI_IGNORED;
        if (expire(session, input->now_ms, step)) break;
        finish_input(session, PET_AI_REASON_RELEASE, input->now_ms, step);
        break;
    case PET_AI_RESPONSE_BEGIN:
        if (session->view.state != PET_AI_THINKING) return PET_AI_IGNORED;
        if (expire(session, input->now_ms, step)) break;
        step->actions = PET_AI_ACTION_START_PLAYBACK;
        state(session, PET_AI_SPEAKING, PET_AI_REASON_RESPONSE, input->now_ms, step);
        break;
    case PET_AI_PLAYBACK_DRAINED:
        if (session->view.state != PET_AI_SPEAKING) return PET_AI_IGNORED;
        step->actions = PET_AI_ACTION_STOP_PLAYBACK | PET_AI_ACTION_CLEAR_AUDIO;
        if (conv_open(session, input->now_ms)) {
            step->actions |= PET_AI_ACTION_START_CAPTURE; /* 会话窗口保持采集，等待 VAD 续轮。 */
            /* 采集即将重启；以当前时刻重置帧锚点，避免窗口首个 TICK 误判输入中断。 */
            session->last_frame_ms = input->now_ms;
            /* 上一轮的语音累计不参与续轮判定，窗口内重新累计。 */
            session->view.speech_ms = session->view.silence_ms = 0;
            session->energy_speech = false;
            state(session, PET_AI_CONVERSATION, PET_AI_REASON_DRAINED, input->now_ms, step);
        } else {
            state(session, PET_AI_IDLE, PET_AI_REASON_DRAINED, input->now_ms, step);
        }
        break;
    case PET_AI_MUTE:
        if (session->view.state == PET_AI_MUTED) return PET_AI_IGNORED;
        cancel_turn(session, step);
        conv_end(session);
        state(session, PET_AI_MUTED, PET_AI_REASON_MUTED, input->now_ms, step);
        break;
    case PET_AI_UNMUTE:
        if (session->view.state != PET_AI_MUTED) return PET_AI_IGNORED;
        state(session, PET_AI_IDLE, PET_AI_REASON_UNMUTED, input->now_ms, step);
        break;
    case PET_AI_CANCEL:
        cancel_turn(session, step);
        conv_end(session);
        if (session->view.state != PET_AI_MUTED)
            state(session, PET_AI_IDLE, PET_AI_REASON_CANCELLED, input->now_ms, step);
        break;
    case PET_AI_AUDIO_FAILED:
    case PET_AI_TRANSPORT_FAILED:
        if (session->view.state != PET_AI_LISTENING && session->view.state != PET_AI_THINKING &&
            session->view.state != PET_AI_SPEAKING && session->view.state != PET_AI_CONVERSATION)
            return PET_AI_IGNORED;
        cancel_turn(session, step);
        conv_end(session);
        state(session, PET_AI_ERROR, input->kind == PET_AI_AUDIO_FAILED ?
              PET_AI_REASON_AUDIO_ERROR : PET_AI_REASON_TRANSPORT_ERROR, input->now_ms, step);
        break;
    case PET_AI_TICK:
        expire(session, input->now_ms, step);
        break;
    default:
        return PET_AI_IGNORED;
    }
    session->last_now_ms = input->now_ms;
    return PET_AI_OK;
}

pet_ai_result_t pet_ai_session_audio(pet_ai_session_t *session,
                                    const pet_ai_audio_input_t *input, pet_ai_step_t *step)
{
    const pet_ai_result_t ready = prepare(session, step);
    if (ready != PET_AI_OK) return ready;
    if (input == NULL || input->pcm == NULL || input->samples != PET_AUDIO_CAPTURE_SAMPLES ||
        input->activity < PET_AI_ACTIVITY_ENERGY || input->activity > PET_AI_ACTIVITY_SILENCE ||
        input->turn_id == 0)
        return PET_AI_INVALID;
    /* 先拒绝旧轮次，迟到的云端包/PCM 不能推进新轮次的时钟或计数。 */
    if (input->turn_id != session->view.turn_id || input->now_ms < session->last_now_ms)
        return PET_AI_STALE;
    if (session->view.state != PET_AI_LISTENING && session->view.state != PET_AI_CONVERSATION)
        return PET_AI_IGNORED;
    if (input->now_ms - session->last_frame_ms < PET_AUDIO_FRAME_MS) return PET_AI_STALE;
    if (input->now_ms - session->last_frame_ms > PET_AI_FRAME_GAP_MS) {
        expire(session, input->now_ms, step);
        session->last_now_ms = input->now_ms;
        return PET_AI_OK;
    }
    if (session->view.state == PET_AI_LISTENING &&
        input->now_ms - session->listen_start_ms > PET_AI_MAX_LISTEN_MS) {
        finish_input(session, PET_AI_REASON_LIMIT, input->now_ms, step);
        session->last_now_ms = input->now_ms;
        return PET_AI_OK;
    }
    pet_audio_level_t level;
    pet_audio_pcm_measure(input->pcm, input->samples, &level);
    if (input->activity == PET_AI_ACTIVITY_SPEECH) session->energy_speech = true;
    else if (input->activity == PET_AI_ACTIVITY_SILENCE) session->energy_speech = false;
    else if (level.rms >= session->config.energy_on_rms) session->energy_speech = true;
    else if (level.rms <= session->config.energy_off_rms) session->energy_speech = false;
    if (session->energy_speech) {
        session->view.speech_ms += PET_AUDIO_FRAME_MS;
        session->view.silence_ms = 0;
    } else {
        session->view.silence_ms += PET_AUDIO_FRAME_MS;
        if (session->view.silence_ms >= PET_AI_END_SILENCE_MS &&
            session->view.speech_ms < PET_AI_MIN_SPEECH_MS) session->view.speech_ms = 0;
    }
    session->view.level_pct = level.level_pct;
    if (session->view.state == PET_AI_CONVERSATION) {
        /* 会话窗口只做 VAD 监听：不转发 PCM，检测到足够语音后原采集续开一轮。 */
        step->actions = 0;
        if (session->view.speech_ms >= PET_AI_CONV_RESUME_MS) {
            if (session->view.turn_id == UINT32_MAX) return PET_AI_ID_EXHAUSTED;
            ++session->view.turn_id;
            session->view.speech_ms = session->view.silence_ms = 0;
            session->view.push_to_talk = false;
            session->listen_start_ms = input->now_ms;
            session->energy_speech = true;
            step->actions = PET_AI_ACTION_BEGIN_TURN;
            state(session, PET_AI_LISTENING, PET_AI_REASON_CONV_SPEECH, input->now_ms, step);
        }
        session->last_now_ms = session->last_frame_ms = input->now_ms;
        step->view = session->view;
        return PET_AI_OK;
    }
    step->actions = PET_AI_ACTION_FORWARD_PCM;
    if (input->now_ms - session->listen_start_ms >= PET_AI_MAX_LISTEN_MS) {
        finish_input(session, PET_AI_REASON_LIMIT, input->now_ms, step);
    } else if (!session->view.push_to_talk && session->view.speech_ms >= PET_AI_MIN_SPEECH_MS &&
        session->view.silence_ms >= PET_AI_END_SILENCE_MS) {
        finish_input(session, PET_AI_REASON_SILENCE, input->now_ms, step);
    }
    session->last_now_ms = session->last_frame_ms = input->now_ms;
    step->view = session->view;
    return PET_AI_OK;
}
