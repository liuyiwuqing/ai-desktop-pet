/* 桌面入口复用真实成长服务；这里只实现 POSIX 存储/锁/时间边界和命令行。
 * 文件原子替换用于进程重启验证，不代表 Flash 掉电/磨损验证。 */
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "freertos/semphr.h"
#include "pet/event_bus.h"
#include "pet/storage.h"
#include "svc_growth/api.h"

static int state_dir = -1;
static uint64_t simulated_ms;
static uint32_t simulated_epoch;
static bool climate_stream;
static pet_climate_reading_t simulated_climate;

static pet_growth_result_t observe_climate(void)
{
    simulated_climate.timestamp_ms = (uint32_t)simulated_ms;
    return pet_growth_observe_climate(&simulated_climate);
}

static bool valid_key(const char *ns, const char *key)
{
    return ns && key && strcmp(ns, "pet_growth") == 0 &&
        (strcmp(key, "snap0") == 0 || strcmp(key, "snap1") == 0 || strcmp(key, "active") == 0);
}
esp_err_t pet_storage_init(void) { return state_dir >= 0 ? ESP_OK : ESP_FAIL; }
esp_err_t pet_storage_load(const char *ns, const char *key, void *out, size_t capacity, size_t *size)
{
    if (!valid_key(ns, key) || !out || !size) return ESP_ERR_INVALID_ARG;
    int fd = openat(state_dir, key, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return errno == ENOENT ? ESP_ERR_NVS_NOT_FOUND : ESP_FAIL;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || (uint64_t)st.st_size > capacity) {
        close(fd);
        return ESP_ERR_INVALID_SIZE;
    }
    size_t count = 0;
    while (count < (size_t)st.st_size) {
        ssize_t got = read(fd, (uint8_t *)out + count, (size_t)st.st_size - count);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) { close(fd); return ESP_FAIL; }
        count += (size_t)got;
    }
    close(fd);
    *size = count;
    return ESP_OK;
}
esp_err_t pet_storage_save(const char *ns, const char *key, const void *data, size_t size)
{
    if (!valid_key(ns, key) || !data || !size) return ESP_ERR_INVALID_ARG;
    char temporary[32];
    snprintf(temporary, sizeof(temporary), ".%s.tmp", key);
    int fd = openat(state_dir, temporary, O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return ESP_FAIL;
    size_t count = 0;
    while (count < size) {
        ssize_t sent = write(fd, (const uint8_t *)data + count, size - count);
        if (sent < 0 && errno == EINTR) continue;
        if (sent <= 0) { close(fd); unlinkat(state_dir, temporary, 0); return ESP_FAIL; }
        count += (size_t)sent;
    }
    int synced = fsync(fd);
    int closed = close(fd);
    if (synced != 0 || closed != 0 || renameat(state_dir, temporary, state_dir, key) != 0) {
        unlinkat(state_dir, temporary, 0);
        return ESP_FAIL;
    }
    (void)fsync(state_dir); /* rename 为提交点；目录 fsync 在部分桌面文件系统不支持。 */
    return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *mutex = malloc(sizeof(*mutex));
    if (!mutex) return NULL;
    if (pthread_mutex_init(mutex, NULL) != 0) { free(mutex); return NULL; }
    return mutex;
}
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t timeout)
{
    return (timeout == 0 ? pthread_mutex_trylock(mutex) : pthread_mutex_lock(mutex)) == 0;
}
int xSemaphoreGive(SemaphoreHandle_t mutex) { return pthread_mutex_unlock(mutex) == 0; }
void vSemaphoreDelete(SemaphoreHandle_t mutex) { pthread_mutex_destroy(mutex); free(mutex); }
esp_err_t pet_event_bus_publish(const pet_event_t *event)
{
    return pet_event_is_valid(event) ? ESP_OK : ESP_ERR_INVALID_ARG;
}
static uint64_t monotonic(void *context) { (void)context; return simulated_ms; }
static uint32_t epoch(void *context) { (void)context; return simulated_epoch; }

static void show(void)
{
    pet_growth_view_t view;
    pet_growth_result_t result = pet_growth_get_snapshot(&view);
    if (result != PET_GROWTH_OK) { printf("%s\n", pet_growth_result_name(result)); return; }
    printf("level=%u exp=%" PRIu32 "/%" PRIu32 " coins=%" PRIu32
           " intimacy=%u mood=%d energy=%u hunger=%u health=%u state=%s\n",
           view.level, view.exp, view.exp_to_next, view.coins, view.intimacy, view.mood,
           view.energy, view.hunger, view.health, pet_growth_state_name(view.state));
    printf("cooldown(feed/pet/play)=%u/%u/%us pet_today=%u next_id=%" PRIu64
           " revision=%" PRIu32 " time=%s save=%s\n", view.feed_cooldown_s,
           view.pet_cooldown_s, view.play_cooldown_s, view.pet_count_today, view.next_request_id,
           view.revision, view.time_ready ? "ready" : "waiting", view.unsaved ? "pending" : "saved");
    printf("daily: feed=%u/1 pet=%u/3 play=%u/1 claimed=%u day=%" PRIu32 "\n",
           view.task_progress[0], view.task_progress[1], view.task_progress[2], view.task_claimed, view.task_day);
    printf("achievements=%u environment=%s\n", view.achievements,
           pet_growth_environment_name((pet_growth_environment_t)view.environment));
    if (view.save_recovered) puts("存档损坏：已退回上一有效版本，最后一次提交可能丢失。");
    if (!view.storage_ready) puts("提交结果未能确认：请重新启动，沿用原请求编号恢复。");
}
static void show_achievements(void)
{
    pet_growth_view_t view;
    pet_growth_result_t result = pet_growth_get_snapshot(&view);
    if (result != PET_GROWTH_OK) { printf("%s\n", pet_growth_result_name(result)); return; }
    printf("首次照料 · %u 金币 · %s\n", PET_GROWTH_CARE_COINS,
           view.achievements & PET_GROWTH_ACHIEVEMENT_CARE ? "已达成" : "完成一次喂食/摸头/玩耍");
    printf("亲密伙伴 · %u 金币 · %s（亲密 %u/%u）\n", PET_GROWTH_FRIEND_COINS,
           view.achievements & PET_GROWTH_ACHIEVEMENT_FRIEND ? "已达成" : "未达成",
           view.intimacy, PET_GROWTH_FRIEND_TARGET);
    printf("成长起步 · %u 金币 · %s（等级 %u/%u）\n", PET_GROWTH_LEVEL_COINS,
           view.achievements & PET_GROWTH_ACHIEVEMENT_LEVEL ? "已达成" : "未达成",
           view.level, PET_GROWTH_LEVEL_TARGET);
}
static bool parse_number(const char *text, uint64_t max, uint64_t *out)
{
    if (!text || !*text || *text == '-') return false;
    errno = 0;
    char *end;
    unsigned long long value = strtoull(text, &end, 10);
    if (errno || *end || value > max) return false;
    *out = value;
    return true;
}
static void help(void)
{
    puts("命令：status | achievements 成就 | feed 喂食 | pet 摸头 | play 玩耍 | sleep | wake | wait 秒数 | sync Unix秒 | claim-feed | claim-pet | claim-play | climate 温度 湿度 | climate-off | climate-loss | flush | quit");
    puts("climate 开始每 5 秒模拟滤波后读数；climate-off 发送失效读数；climate-loss 停止更新，等候 30 秒过期。");
    puts("wait 推进模拟时间（单次最多七天）；重新启动沿用 --state-dir 存档，--epoch 指定开机时间。");
}

int main(int argc, char **argv)
{
    const char *directory = NULL;
    simulated_epoch = (uint32_t)time(NULL);
    for (int i = 1; i < argc; ++i) {
        uint64_t value;
        if (strcmp(argv[i], "--state-dir") == 0 && i + 1 < argc) directory = argv[++i];
        else if (strcmp(argv[i], "--epoch") == 0 && i + 1 < argc && parse_number(argv[i + 1], UINT32_MAX, &value)) {
            simulated_epoch = (uint32_t)value;
            ++i;
        } else { fprintf(stderr, "usage: %s --state-dir DIR [--epoch UNIX_SECONDS]\n", argv[0]); return 2; }
    }
    if (!directory) { fputs("缺少 --state-dir\n", stderr); return 2; }
    if (mkdir(directory, 0700) != 0 && errno != EEXIST) { perror(directory); return 1; }
    state_dir = open(directory, O_RDONLY | O_DIRECTORY);
    if (state_dir < 0) { perror(directory); return 1; }
    int lock_fd = openat(state_dir, ".lock", O_CREAT | O_WRONLY | O_NOFOLLOW, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        fputs("存档目录正被使用或无法加锁\n", stderr);
        if (lock_fd >= 0) close(lock_fd);
        close(state_dir);
        return 1;
    }
    const pet_growth_clock_t clock = {monotonic, epoch, NULL, 480};
    pet_growth_result_t result = pet_growth_init(&clock);
    if (result != PET_GROWTH_OK) {
        fprintf(stderr, "init: %s（保留原存档）\n", pet_growth_result_name(result));
        close(lock_fd); close(state_dir);
        return 1;
    }
    bool interactive = isatty(STDIN_FILENO);
    if (interactive) help();
    show();
    char line[256];
    int exit_code = 0;
    for (;;) {
        if (interactive) { fputs("> ", stdout); fflush(stdout); }
        if (!fgets(line, sizeof(line), stdin)) break;
        char command[24], argument[64], extra[64], trailing[8];
        int fields = sscanf(line, "%23s %63s %63s %7s", command, argument, extra, trailing);
        if (fields <= 0) continue;
        uint64_t value;
        result = PET_GROWTH_INVALID;
        if (fields == 1 && strcmp(command, "quit") == 0) break;
        if (fields == 1 && strcmp(command, "help") == 0) { help(); continue; }
        if (fields == 1 && strcmp(command, "status") == 0) { show(); continue; }
        if (fields == 1 && strcmp(command, "achievements") == 0) { show_achievements(); continue; }
        if (fields == 2 && strcmp(command, "wait") == 0 && parse_number(argument, 604800, &value)) {
            result = PET_GROWTH_OK;
            while (value && result == PET_GROWTH_OK) {
                uint32_t limit = climate_stream ? 5 : 60;
                uint32_t step = value > limit ? limit : (uint32_t)value;
                simulated_ms += (uint64_t)step * 1000;
                if (simulated_epoch) simulated_epoch = simulated_epoch > UINT32_MAX - step ? UINT32_MAX : simulated_epoch + step;
                result = pet_growth_tick();
                if (result == PET_GROWTH_OK && climate_stream) result = observe_climate();
                value -= step;
            }
        } else if (fields == 2 && strcmp(command, "sync") == 0 && parse_number(argument, UINT32_MAX, &value)) {
            simulated_epoch = (uint32_t)value;
            result = pet_growth_tick();
        } else if (fields == 1 && strcmp(command, "flush") == 0) {
            result = pet_growth_flush();
        } else if (fields == 3 && strcmp(command, "climate") == 0) {
            char *temp_end, *humidity_end;
            float temp = strtof(argument, &temp_end), humidity = strtof(extra, &humidity_end);
            if (temp_end != argument && !*temp_end && humidity_end != extra && !*humidity_end &&
                isfinite(temp) && isfinite(humidity) && temp >= PET_CLIMATE_TEMP_MIN_C &&
                temp <= PET_CLIMATE_TEMP_MAX_C && humidity >= 0 && humidity <= 100) {
                simulated_climate = (pet_climate_reading_t){.temperature_c = temp, .humidity_pct = humidity,
                    .quality = 100, .valid = true};
                ++simulated_ms; /* 连续输入代表新采样，不能沿用同一毫秒被判为重放。 */
                result = observe_climate();
                climate_stream = result == PET_GROWTH_OK;
            }
        } else if (fields == 1 && strcmp(command, "climate-off") == 0) {
            climate_stream = false;
            simulated_climate.valid = false;
            ++simulated_ms;
            result = observe_climate();
        } else if (fields == 1 && strcmp(command, "climate-loss") == 0) {
            climate_stream = false;
            result = PET_GROWTH_OK;
        } else if (fields == 1) {
            static const char *actions[] = {"feed", "pet", "play", "sleep", "wake", "claim-feed", "claim-pet", "claim-play"};
            for (unsigned i = 0; i < sizeof(actions) / sizeof(actions[0]); ++i) {
                if (strcmp(command, actions[i]) != 0) continue;
                pet_growth_view_t view;
                result = pet_growth_get_snapshot(&view);
                if (result == PET_GROWTH_OK) {
                    pet_growth_request_t request = {view.next_request_id, simulated_ms, (pet_growth_action_t)i, view.task_day};
                    result = pet_growth_interact(&request);
                }
                break;
            }
        }
        printf("%s: %s\n", command, pet_growth_result_name(result));
        show();
        if (result == PET_GROWTH_INVALID || result == PET_GROWTH_STORAGE_ERROR ||
            result == PET_GROWTH_STORAGE_UNCERTAIN) { exit_code = 1; break; }
    }
    result = pet_growth_flush();
    if (result != PET_GROWTH_OK && result != PET_GROWTH_TIME_UNAVAILABLE) {
        fprintf(stderr, "flush: %s\n", pet_growth_result_name(result));
        exit_code = 1;
    }
    pet_growth_close();
    close(lock_fd); close(state_dir);
    return exit_code;
}
