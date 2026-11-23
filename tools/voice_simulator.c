/* 离线开发入口：使用真实会话引擎；PCM、唤醒与云端事件均为显式注入。 */
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "svc_ai_agent/session.h"

static const char *const states[] = {"idle", "listening", "thinking", "speaking", "conversation",
    "muted", "error"};
static const char *const reasons[] = {"none", "wake", "press", "silence", "release", "limit",
    "no_speech", "response", "drained", "cancelled", "muted", "unmuted", "response_timeout",
    "input_gap", "audio_error", "transport_error", "conv_speech", "conv_timeout"};
static const char *const results[] = {"ok", "ignored", "stale", "invalid", "not_ready", "id_exhausted"};
static const char *const actions[] = {"state", "begin", "capture_start", "capture_stop", "forward",
    "end_input", "cancel", "playback_start", "playback_stop", "clear"};

static void show(uint64_t now, pet_ai_result_t result, const pet_ai_step_t *step)
{
    printf("t=%" PRIu64 " result=%s state=%s reason=%s turn=%" PRIu32
           " speech=%u silence=%u level=%u cancelled=%" PRIu32 " actions=",
           now, results[result], states[step->view.state], reasons[step->view.reason],
           step->view.turn_id, step->view.speech_ms, step->view.silence_ms,
           step->view.level_pct, step->cancelled_turn_id);
    bool first = true;
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
        if (step->actions & (1u << i)) {
            printf("%s%s", first ? "" : ",", actions[i]);
            first = false;
        }
    }
    puts(first ? "-" : "");
}

static void help(void)
{
    puts("离线语音模拟：无麦克风、网络或烧录；voice/silence 为合成 PCM，wake/tts/done 为注入事件。");
    puts("wake | press | release [turn] | voice 毫秒 [幅度0..32767] | silence 毫秒 | wait 毫秒");
    puts("tts [turn] | done [turn] | error audio|transport [turn] | cancel | mute | unmute | status | quit");
    puts("音频时长须为20的倍数；wait只推进时钟，不填静音。状态/actions表示控制意图，未实际执行设备操作。");
}

static bool number(const char *text, uint64_t limit, uint64_t *value)
{
    if (text == NULL || *text < '0' || *text > '9') return false;
    errno = 0;
    char *end;
    const unsigned long long parsed = strtoull(text, &end, 10);
    if (errno || *end != '\0' || parsed > limit) return false;
    *value = parsed;
    return true;
}

static bool command(pet_ai_session_t *session, char **args, size_t count, uint64_t *now)
{
    pet_ai_step_t step = {0};
    pet_ai_session_snapshot(session, &step.view);
    if (!strcmp(args[0], "status")) {
        if (count != 1) return false;
        show(*now, PET_AI_OK, &step);
        return true;
    }
    if (!strcmp(args[0], "voice") || !strcmp(args[0], "silence")) {
        const bool voice = !strcmp(args[0], "voice");
        uint64_t duration, magnitude = voice ? 1200 : 0;
        if (count < 2 || count > (voice ? 3u : 2u) || !number(args[1], 60000, &duration) ||
            duration == 0 || duration % PET_AUDIO_FRAME_MS != 0 ||
            (count == 3 && !number(args[2], 32767, &magnitude))) return false;
        if (step.view.state != PET_AI_LISTENING) {
            show(*now, PET_AI_IGNORED, &step);
            return true;
        }
        int16_t pcm[PET_AUDIO_CAPTURE_SAMPLES];
        for (size_t i = 0; i < PET_AUDIO_CAPTURE_SAMPLES; ++i)
            pcm[i] = (int16_t)(i % 2 ? (int)magnitude : -(int)magnitude);
        for (uint64_t elapsed = 0; elapsed < duration; elapsed += PET_AUDIO_FRAME_MS) {
            if (*now > UINT64_MAX - PET_AUDIO_FRAME_MS) return false;
            *now += PET_AUDIO_FRAME_MS;
            const pet_ai_audio_input_t frame = {.turn_id = step.view.turn_id, .now_ms = *now,
                .pcm = pcm, .samples = PET_AUDIO_CAPTURE_SAMPLES, .activity = PET_AI_ACTIVITY_ENERGY};
            const pet_ai_result_t result = pet_ai_session_audio(session, &frame, &step);
            if (result != PET_AI_OK || step.view.state != PET_AI_LISTENING ||
                elapsed + PET_AUDIO_FRAME_MS == duration) show(*now, result, &step);
            if (result != PET_AI_OK || step.view.state != PET_AI_LISTENING) break;
        }
        memset(pcm, 0, sizeof(pcm));
        return true;
    }
    pet_ai_input_t input = {.now_ms = *now};
    size_t turn_arg = 0;
    if (!strcmp(args[0], "wake")) input.kind = PET_AI_WAKE_DETECTED;
    else if (!strcmp(args[0], "press")) input.kind = PET_AI_TALK_PRESS;
    else if (!strcmp(args[0], "release")) { input.kind = PET_AI_TALK_RELEASE; turn_arg = 1; }
    else if (!strcmp(args[0], "tts")) { input.kind = PET_AI_RESPONSE_BEGIN; turn_arg = 1; }
    else if (!strcmp(args[0], "done")) { input.kind = PET_AI_PLAYBACK_DRAINED; turn_arg = 1; }
    else if (!strcmp(args[0], "cancel")) input.kind = PET_AI_CANCEL;
    else if (!strcmp(args[0], "mute")) input.kind = PET_AI_MUTE;
    else if (!strcmp(args[0], "unmute")) input.kind = PET_AI_UNMUTE;
    else if (!strcmp(args[0], "wait")) {
        uint64_t elapsed;
        if (count != 2 || !number(args[1], 60000, &elapsed) || *now > UINT64_MAX - elapsed) return false;
        *now += elapsed;
        input.now_ms = *now;
        input.kind = PET_AI_TICK;
    } else if (!strcmp(args[0], "error")) {
        if (count < 2) return false;
        if (!strcmp(args[1], "audio")) input.kind = PET_AI_AUDIO_FAILED;
        else if (!strcmp(args[1], "transport")) input.kind = PET_AI_TRANSPORT_FAILED;
        else return false;
        turn_arg = 2;
    } else return false;
    if (turn_arg) {
        uint64_t turn = step.view.turn_id;
        if (count < turn_arg || count > turn_arg + 1 ||
            (count > turn_arg && !number(args[turn_arg], UINT32_MAX, &turn))) return false;
        input.turn_id = (uint32_t)turn;
    } else if (input.kind != PET_AI_TICK && count != 1) return false;
    const pet_ai_result_t result = pet_ai_session_handle(session, &input, &step);
    show(*now, result, &step);
    return result != PET_AI_INVALID && result != PET_AI_NOT_READY;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--help")) { help(); return 0; }
    if (argc != 1) { fputs("只接受 --help；命令从标准输入读取。\n", stderr); return 2; }
    pet_ai_session_t session;
    if (pet_ai_session_init(&session, NULL, 0) != PET_AI_OK) return 1;
    help();
    uint64_t now = 0;
    char line[256];
    int errors = 0;
    while (fgets(line, sizeof(line), stdin)) {
        if (!strchr(line, '\n') && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {}
            fputs("命令过长。\n", stderr); errors = 2; continue;
        }
        char *args[5]; size_t count = 0;
        for (char *word = strtok(line, " \t\r\n"); word && count < 5; word = strtok(NULL, " \t\r\n"))
            args[count++] = word;
        if (count == 0) continue;
        if (count == 1 && !strcmp(args[0], "quit")) break;
        if (count == 1 && !strcmp(args[0], "help")) { help(); continue; }
        if (count >= 5 || !command(&session, args, count, &now)) {
            fputs("无效命令或参数；输入 help 查看用法。\n", stderr);
            errors = 2;
        }
    }
    if (ferror(stdin)) return 1;
    return errors;
}
