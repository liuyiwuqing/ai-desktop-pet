#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <math.h>

#include "freertos/semphr.h"
#include "pet/storage.h"
#include "pet/event_bus.h"
#include "svc_growth/api.h"
#include "fixtures/growth-v1.h"
#include "fixtures/growth-v2.h"

/* 只替换存储、锁、时钟和事件边界；引擎、编解码、双槽与服务使用固件实现。 */
static struct { unsigned char data[256]; size_t size; } blobs[3];
static const char *fail_key;
static uint64_t now_ms;
static uint32_t now_epoch = 1800000000;
static unsigned events;
static unsigned writes;
static bool corrupt_write;
static bool error_after_commit;
static const char *fail_read_key;

static unsigned key_index(const char *key)
{
    if (strcmp(key, "snap0") == 0) return 0;
    if (strcmp(key, "snap1") == 0) return 1;
    assert(strcmp(key, "active") == 0);
    return 2;
}
esp_err_t pet_storage_init(void) { return ESP_OK; }
esp_err_t pet_storage_load(const char *ns, const char *key, void *out, size_t cap, size_t *size)
{
    assert(strcmp(ns, "pet_growth") == 0);
    if (fail_read_key && strcmp(key, fail_read_key) == 0) return ESP_FAIL;
    unsigned i = key_index(key);
    if (!blobs[i].size) return ESP_ERR_NVS_NOT_FOUND;
    if (cap < blobs[i].size) return ESP_ERR_INVALID_SIZE;
    *size = blobs[i].size;
    memcpy(out, blobs[i].data, *size);
    return ESP_OK;
}
esp_err_t pet_storage_save(const char *ns, const char *key, const void *data, size_t size)
{
    assert(strcmp(ns, "pet_growth") == 0);
    ++writes;
    if (fail_key && strcmp(key, fail_key) == 0) return ESP_FAIL;
    unsigned i = key_index(key);
    assert(size <= sizeof(blobs[i].data));
    memcpy(blobs[i].data, data, size);
    blobs[i].size = size;
    if (corrupt_write && i < 2) blobs[i].data[24] ^= 1;
    if (error_after_commit && i == 2) return ESP_FAIL;
    return ESP_OK;
}
SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    pthread_mutex_t *mutex = malloc(sizeof(*mutex));
    assert(mutex && pthread_mutex_init(mutex, NULL) == 0);
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
    assert(pet_event_is_valid(event) && event->payload_len == sizeof(pet_growth_view_t));
    pet_growth_view_t view;
    memcpy(&view, event->payload, sizeof(view));
    assert(!view.unsaved);
    ++events;
    return ESP_OK;
}
static uint64_t monotonic(void *context) { (void)context; return now_ms; }
static uint32_t epoch(void *context) { (void)context; return now_epoch; }
static const pet_growth_clock_t clock_source = {monotonic, epoch, NULL, 480};
static pet_growth_view_t snapshot(void)
{
    pet_growth_view_t view;
    assert(pet_growth_get_snapshot(&view) == PET_GROWTH_OK);
    return view;
}
static pet_growth_result_t act(pet_growth_action_t action, uint64_t id)
{
    const pet_growth_request_t request = {id, now_ms, action, action >= PET_GROWTH_CLAIM_FEED ? snapshot().task_day : 0};
    return pet_growth_interact(&request);
}

static void fresh(void)
{
    pet_growth_close();
    memset(blobs, 0, sizeof(blobs));
    fail_key = NULL;
    corrupt_write = false;
    error_after_commit = false;
    fail_read_key = NULL;
    now_ms = 0;
    now_epoch = 1800000000;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
}

static void check_backup_restore(void)
{
    fresh();
    assert(act(PET_GROWTH_FEED,1)==PET_GROWTH_OK);
    unsigned char backup[PET_GROWTH_BACKUP_MAX];size_t bytes;uint32_t revision;
    assert(pet_growth_export(backup,sizeof(backup),&bytes,&revision)==PET_GROWTH_OK);
    assert(bytes==104 && revision==snapshot().revision);
    assert(act(PET_GROWTH_PET,2)==PET_GROWTH_OK);
    assert(pet_growth_import(backup,bytes,revision)==PET_GROWTH_CONFLICT);
    revision=snapshot().revision;
    fail_key="active";
    assert(pet_growth_import(backup,bytes,revision)==PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().next_request_id==3);
    fail_key=NULL;
    assert(pet_growth_import(backup,bytes,revision)==PET_GROWTH_OK);
    assert(snapshot().exp==8 && snapshot().next_request_id==3);
    assert(act(PET_GROWTH_FEED,1)==PET_GROWTH_DUPLICATE);
    backup[24]^=1;
    assert(pet_growth_import(backup,bytes,snapshot().revision)==PET_GROWTH_CORRUPT_SAVE);
    pet_growth_close();
    assert(pet_growth_init(&clock_source)==PET_GROWTH_OK);
    assert(snapshot().exp==8 && snapshot().next_request_id==3);
}

static void check_first_care_achievement(void)
{
    fresh();
    assert(snapshot().achievements == 0);
    fail_key = "active";
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().achievements == 0 && snapshot().coins == 20);
    fail_key = NULL;
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE);
    assert(snapshot().coins == 25 && snapshot().exp == 8);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_DUPLICATE);
    assert(snapshot().coins == 25);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE && snapshot().coins == 25);
}

static void check_achievement_thresholds(void)
{
    fresh();
    for (unsigned i = 1; i <= 9; ++i) {
        assert(act(PET_GROWTH_PET, i) == PET_GROWTH_OK);
        now_ms += 10000; now_epoch += 10;
    }
    assert(snapshot().intimacy == 19 && snapshot().coins == 30);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE);
    assert(act(PET_GROWTH_PET, 10) == PET_GROWTH_OK);
    assert(snapshot().intimacy == 20 && snapshot().coins == 50 && snapshot().achievements == 3);
    assert(snapshot().state == PET_GROWTH_EXCITED);
    now_ms += 3000;
    assert(snapshot().state != PET_GROWTH_EXCITED);
    now_ms += 7000; now_epoch += 10;
    assert(act(PET_GROWTH_PET, 11) == PET_GROWTH_OK && snapshot().coins == 50);
    for (unsigned i = 12; i <= 651; ++i) {
        now_ms += 10000; now_epoch += 10;
        assert(act(PET_GROWTH_PET, i) == PET_GROWTH_OK);
    }
    /* 前 20 次各 2 EXP，其后各 1；累计 671 EXP，恰好距 5 级一分。 */
    assert(snapshot().level == 4 && snapshot().exp == 271 && snapshot().coins == 50);
    now_ms += 10000; now_epoch += 10;
    assert(act(PET_GROWTH_PET, 652) == PET_GROWTH_OK);
    assert(snapshot().level == 5 && snapshot().exp == 0 && snapshot().coins == 80);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_ALL);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_ALL && snapshot().coins == 80);
    /* 换日和长期衰减不清除成就；重新跨过亲密门槛也不会再次发奖。 */
    for (unsigned day = 0; day < 82; ++day) {
        for (unsigned part = 0; part < 3; ++part) {
            now_ms += 28800000; now_epoch += 28800;
            assert(pet_growth_tick() == PET_GROWTH_OK);
        }
    }
    assert(snapshot().intimacy == 18 && snapshot().achievements == 7);
    assert(act(PET_GROWTH_PET, 653) == PET_GROWTH_OK);
    now_ms += 10000; now_epoch += 10;
    assert(act(PET_GROWTH_PET, 654) == PET_GROWTH_OK);
    assert(snapshot().intimacy == 20 && snapshot().coins == 80);
}

static void check_achievement_uncertain_commit(void)
{
    fresh();
    fail_read_key = "active";
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_STORAGE_UNCERTAIN);
    assert(!snapshot().storage_ready && snapshot().achievements == 0 && snapshot().coins == 20);
    pet_growth_close(); fail_read_key = NULL;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE && snapshot().coins == 30);
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_DUPLICATE);
    assert(snapshot().coins == 30);
}

static void check_achievement_v2_migration(void)
{
    fresh(); pet_growth_close();
    /* 成对载入真实 v2 快照与提交记录，不依赖新编码器重新生成旧格式。 */
    memcpy(blobs[0].data, growth_v2, sizeof(growth_v2));
    blobs[0].size = sizeof(growth_v2);
    memcpy(blobs[2].data, growth_v2_active, sizeof(growth_v2_active));
    now_epoch = 1800000020;
    unsigned before = writes;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(writes == before); /* 读旧档不自动修改版本或推断历史成就。 */
    assert(snapshot().coins == 33 && snapshot().exp == 74 && snapshot().task_claimed == 7);
    assert(snapshot().task_progress[1] == 3 && snapshot().achievements == 0);
    assert(act(PET_GROWTH_CLAIM_PLAY, 9) == PET_GROWTH_ALREADY_CLAIMED);
    assert(snapshot().achievements == 0 && snapshot().coins == 33);
    now_ms += 10000; now_epoch += 10;
    assert(act(PET_GROWTH_PET, 9) == PET_GROWTH_OK);
    assert(snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE && snapshot().coins == 43);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().achievements == 1 && snapshot().coins == 43 && snapshot().task_claimed == 7);

    fresh(); pet_growth_close();
    memcpy(blobs[0].data, growth_v2_milestones, sizeof(growth_v2_milestones));
    blobs[0].size = sizeof(growth_v2_milestones);
    memcpy(blobs[2].data, growth_v2_active, sizeof(growth_v2_active));
    now_epoch = 1800000020;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().level == 5 && snapshot().intimacy == 20 && snapshot().achievements == 0);
    assert(act(PET_GROWTH_SLEEP, 9) == PET_GROWTH_OK);
    assert(snapshot().achievements == 6 && snapshot().coins == 83);
    assert(act(PET_GROWTH_WAKE, 10) == PET_GROWTH_OK && snapshot().coins == 83);
    now_ms += 10000; now_epoch += 10;
    assert(act(PET_GROWTH_PET, 11) == PET_GROWTH_OK);
    assert(snapshot().achievements == 7 && snapshot().coins == 93);
}

static pet_growth_result_t climate(float temperature, float humidity, unsigned quality, bool valid)
{
    pet_climate_reading_t reading = {.temperature_c = temperature, .humidity_pct = humidity,
        .timestamp_ms = (uint32_t)now_ms, .quality = (uint8_t)quality, .valid = valid};
    return pet_growth_observe_climate(&reading);
}

static void check_climate_exposure(void)
{
    fresh();
    assert(act(PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
    assert(snapshot().environment == PET_GROWTH_ENV_HOT);
    for (unsigned second = 1; second <= 3600; ++second) {
        now_ms += 1000; now_epoch += 1;
        if (second % 5 == 0) assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
        assert(pet_growth_tick() == PET_GROWTH_OK);
    }
    assert(snapshot().mood == 19 && snapshot().health == 100 && snapshot().coins == 20);
    assert(snapshot().state == PET_GROWTH_SLEEPING); /* 环境表现不覆盖睡眠等业务优先级。 */
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().mood == 19 && snapshot().environment == PET_GROWTH_ENV_NONE);
}

static void check_climate_quality_and_expiry(void)
{
    fresh();
    assert(pet_growth_observe_climate(NULL) == PET_GROWTH_INVALID);
    const struct { float t, h; unsigned quality; bool valid; pet_growth_environment_t effect; } cases[] = {
        {10, 30, 100, true, PET_GROWTH_ENV_NONE}, {30, 80, 100, true, PET_GROWTH_ENV_NONE},
        {9.9f, 90, 100, true, PET_GROWTH_ENV_COLD}, {30.1f, 10, 100, true, PET_GROWTH_ENV_HOT},
        {20, 29.9f, 100, true, PET_GROWTH_ENV_DRY}, {20, 80.1f, 100, true, PET_GROWTH_ENV_HUMID},
        {31, 90, 49, true, PET_GROWTH_ENV_NONE}, {31, 90, 50, true, PET_GROWTH_ENV_HOT},
        {31, 90, 101, true, PET_GROWTH_ENV_NONE}, {31, 90, 100, false, PET_GROWTH_ENV_NONE},
        {NAN, 90, 100, true, PET_GROWTH_ENV_NONE}, {20, INFINITY, 100, true, PET_GROWTH_ENV_NONE},
        {-11, 90, 100, true, PET_GROWTH_ENV_NONE}, {20, 101, 100, true, PET_GROWTH_ENV_NONE},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        now_ms += 10;
        assert(climate(cases[i].t, cases[i].h, cases[i].quality, cases[i].valid) == PET_GROWTH_OK);
        assert(snapshot().environment == cases[i].effect);
    }
    now_ms += 10;
    pet_climate_reading_t invalid = {.temperature_c = 31, .humidity_pct = 90,
        .timestamp_ms = (uint32_t)now_ms, .quality = 100, .valid = true, .error_code = PET_CLIMATE_ERR_CHECKSUM};
    assert(pet_growth_observe_climate(&invalid) == PET_GROWTH_OK);
    assert(snapshot().environment == PET_GROWTH_ENV_NONE);

    fresh();
    assert(act(PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
    pet_climate_reading_t replay = {.temperature_c = 31, .humidity_pct = 90,
        .timestamp_ms = 0, .quality = 100, .valid = true};
    now_ms = 29999;
    assert(pet_growth_observe_climate(&replay) == PET_GROWTH_DUPLICATE);
    assert(snapshot().environment == PET_GROWTH_ENV_HOT);
    now_ms = 30000;
    assert(snapshot().environment == PET_GROWTH_ENV_NONE);
    assert(pet_growth_observe_climate(&replay) == PET_GROWTH_STALE_REQUEST);
    now_ms = 7200000; now_epoch += 7200;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().mood == 19);
    /* 一条读数最多覆盖 30 秒，不能被长时间未调度外推成两小时暴露。 */
    replay.timestamp_ms = (uint32_t)now_ms + 1;
    assert(pet_growth_observe_climate(&replay) == PET_GROWTH_STALE_REQUEST);
    assert(snapshot().environment == PET_GROWTH_ENV_NONE);
}

static void check_climate_subsecond_and_restore(void)
{
    fresh();
    assert(act(PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    for (unsigned half = 0; half < 14400; ++half) {
        assert(climate(half % 2 ? 20 : 31, 50, 100, true) == PET_GROWTH_OK);
        now_ms += 500;
        if (half % 2) now_epoch += 1;
        assert(pet_growth_tick() == PET_GROWTH_OK);
    }
    assert(snapshot().mood == 19); /* 两小时中一半不适，子秒更新不能吞掉一小时暴露。 */
    assert(pet_growth_flush() == PET_GROWTH_OK);
    pet_growth_close(); now_epoch += 7200;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().mood == 19 && snapshot().environment == PET_GROWTH_ENV_NONE);

    fresh();
    now_ms = UINT32_MAX - 10000u;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
    now_ms += 15000;
    assert(climate(20, 20, 100, true) == PET_GROWTH_OK);
    assert(snapshot().environment == PET_GROWTH_ENV_DRY);
    pet_climate_reading_t old = {.temperature_c = 5, .humidity_pct = 50, .timestamp_ms = (uint32_t)now_ms - 1,
        .quality = 100, .valid = true};
    assert(pet_growth_observe_climate(&old) == PET_GROWTH_STALE_REQUEST);
    assert(snapshot().environment == PET_GROWTH_ENV_DRY);

    /* 32 位采样时钟相隔半圈以上时，只靠差值比较会永久拒绝新读数。 */
    now_ms += UINT32_C(0x80000000);
    assert(climate(5, 50, 100, true) == PET_GROWTH_OK);
    assert(snapshot().environment == PET_GROWTH_ENV_COLD);
}

static void check_climate_storage_failures(void)
{
    fresh();
    assert(act(PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    unsigned before_writes = writes, before_events = events;
    assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
    assert(writes == before_writes && events == before_events); /* 采样只更新 RAM。 */
    now_ms = 30000; now_epoch += 30;
    fail_key = "active";
    assert(act(PET_GROWTH_WAKE, 2) == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().mood == 19 && snapshot().coins == 20 && snapshot().achievements == 0);
    assert(snapshot().state == PET_GROWTH_SLEEPING && snapshot().next_request_id == 2);
    fail_key = NULL;
    assert(act(PET_GROWTH_WAKE, 2) == PET_GROWTH_OK);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().mood == 19 && snapshot().environment == PET_GROWTH_ENV_NONE);

    fresh();
    assert(climate(31, 90, 100, true) == PET_GROWTH_OK);
    fail_read_key = "active";
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_STORAGE_UNCERTAIN);
    before_writes = writes;
    now_ms += 1;
    assert(climate(5, 50, 100, true) == PET_GROWTH_STORAGE_UNCERTAIN);
    assert(snapshot().environment == PET_GROWTH_ENV_HOT && writes == before_writes);
}

static void check_time_and_recovery(void)
{
    fresh();
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_SLEEP, 2) == PET_GROWTH_OK);
    pet_growth_close();
    now_epoch += 3 * 86400;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    /* 离线三天只结算八小时睡眠：饱食 100-16，精力恢复封顶。 */
    assert(snapshot().hunger == 84 && snapshot().energy == 100);
    uint32_t rev = snapshot().revision;
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().hunger == 84 && snapshot().revision == rev);
    pet_growth_close();
    uint32_t synchronized_epoch = now_epoch;
    now_epoch = 0;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(!snapshot().time_ready);
    now_ms = 3600000;
    assert(pet_growth_tick() == PET_GROWTH_TIME_UNAVAILABLE);
    assert(act(PET_GROWTH_WAKE, 3) == PET_GROWTH_TIME_UNAVAILABLE);
    now_epoch = synchronized_epoch + 3600;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().time_ready && snapshot().hunger == 82);
    assert(act(PET_GROWTH_WAKE, 3) == PET_GROWTH_OK);
    /* 活跃运行的墙钟回拨/跳跃均不能让 30 分钟重复结算。 */
    now_ms += 1800000;
    now_epoch = synchronized_epoch - 86400;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().hunger == 80 && snapshot().energy == 98);
    now_epoch = synchronized_epoch + 10 * 86400;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().hunger == 80);
    assert(act(PET_GROWTH_FEED, 4) == PET_GROWTH_OK);
    now_ms += 1800000;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().hunger == 98);
    pet_growth_request_t stale = {5, now_ms - 300001, PET_GROWTH_PLAY, 0};
    assert(pet_growth_interact(&stale) == PET_GROWTH_STALE_REQUEST);
    assert(snapshot().next_request_id == 5);

    fresh();
    now_ms = 299000;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().unsaved);
    assert(snapshot().revision == 1);
    fail_key = "active";
    now_ms = 300000;
    assert(pet_growth_tick() == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().unsaved && snapshot().revision == 1);
    unsigned attempts = writes;
    now_ms += 1000;
    assert(pet_growth_tick() == PET_GROWTH_OK && writes == attempts);
    fail_key = NULL;
    assert(pet_growth_flush() == PET_GROWTH_OK && !snapshot().unsaved);
    assert(snapshot().revision == 2);

    /* 新档未对时可本地互动；首次对时不能抹掉每日次数，也不重复算在线时间。 */
    fresh(); pet_growth_close(); memset(blobs, 0, sizeof(blobs)); now_epoch = 0;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_OK);
    now_ms = 3600000;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().hunger == 66);
    now_epoch = 1800000000;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().hunger == 66 && snapshot().pet_count_today == 1);
    assert(pet_growth_flush() == PET_GROWTH_OK);
    pet_growth_close();
    now_epoch -= 86400;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().hunger == 66 && snapshot().pet_count_today == 1);

    fresh();
    assert(act(PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    for (unsigned hour = 0; hour < 96; ++hour) {
        now_ms += 3600000;
        now_epoch += 3600;
        assert(pet_growth_tick() == PET_GROWTH_OK);
    }
    int16_t negative_mood = snapshot().mood;
    assert(negative_mood < -30);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().mood == negative_mood); /* 有符号心情必须完整跨存档恢复。 */
}

static void check_storage_failures(void)
{
    fresh();
    /* 驱动返回错误不一定代表没写入；读回已提交指针后应确认成功。 */
    error_after_commit = true;
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(snapshot().coins == 25);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_DUPLICATE);
    error_after_commit = false;
    fail_read_key = "active";
    assert(act(PET_GROWTH_PLAY, 2) == PET_GROWTH_STORAGE_UNCERTAIN);
    assert(!snapshot().storage_ready);
    unsigned count = writes;
    assert(act(PET_GROWTH_PET, 2) == PET_GROWTH_STORAGE_UNCERTAIN && writes == count);
    pet_growth_close(); fail_read_key = NULL;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().storage_ready && snapshot().coins == 28);
    assert(act(PET_GROWTH_PLAY, 2) == PET_GROWTH_DUPLICATE);
    fresh();
    corrupt_write = true;
    unsigned before = events;
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().coins == 20 && events == before);
    corrupt_write = false;
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    pet_growth_close();
    /* 活动槽损坏可回退上一已提交代；下一次失败仍保护实际恢复槽。 */
    blobs[1].data[24] ^= 1;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().coins == 20);
    assert(snapshot().save_recovered);
    fail_key = "active";
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_STORAGE_ERROR);
    pet_growth_close(); fail_key = NULL;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().coins == 20 && snapshot().exp == 0);
    assert(act(PET_GROWTH_PET, 1) == PET_GROWTH_OK);
    pet_growth_close();
    blobs[0].data[24] ^= 1;
    blobs[1].data[24] ^= 1;
    unsigned before_writes = writes;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_CORRUPT_SAVE);
    assert(writes == before_writes);
    assert(pet_growth_get_snapshot(&(pet_growth_view_t){0}) == PET_GROWTH_NOT_READY);

    fresh();
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    pet_growth_close();
    blobs[0].data[4] = 99; /* 非活动槽的未知新版本也禁止覆盖。 */
    before_writes = writes;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_UNKNOWN_VERSION);
    assert(writes == before_writes);

    fresh();
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    pet_growth_close();
    blobs[2].size = 0;
    before_writes = writes;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_CORRUPT_SAVE);
    assert(writes == before_writes); /* 不能把游离候选自动视为已提交。 */
}

static void *concurrent_pet(void *result)
{
    *(pet_growth_result_t *)result = act(PET_GROWTH_PET, 1);
    return NULL;
}

static void check_concurrency(void)
{
    fresh();
    pthread_t threads[2];
    pet_growth_result_t results[2];
    for (unsigned i = 0; i < 2; ++i) assert(pthread_create(&threads[i], NULL, concurrent_pet, &results[i]) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert((results[0] == PET_GROWTH_OK && results[1] == PET_GROWTH_DUPLICATE) ||
           (results[1] == PET_GROWTH_OK && results[0] == PET_GROWTH_DUPLICATE));
    assert(snapshot().exp == 2 && snapshot().pet_count_today == 1);
    assert(snapshot().coins == 30 && snapshot().achievements == PET_GROWTH_ACHIEVEMENT_CARE);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_CONFLICT);
    assert(act(PET_GROWTH_FEED, 3) == PET_GROWTH_CONFLICT);
}

static void check_level_up_transient(void)
{
    fresh();
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    for (unsigned i = 0; i < 5; ++i) {
        assert(act(PET_GROWTH_PLAY, i + 2) == PET_GROWTH_OK);
        if (i < 4) { now_ms += 900000; now_epoch += 900; }
    }
    assert(snapshot().level == 2 && snapshot().exp == 3);
    assert(snapshot().state == PET_GROWTH_EXCITED);
    now_ms += 2999;
    assert(snapshot().state == PET_GROWTH_EXCITED);
    now_ms += 1;
    assert(snapshot().state != PET_GROWTH_EXCITED);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().level == 2 && snapshot().state != PET_GROWTH_EXCITED);
}

static pet_growth_request_t concurrent_claim_request;
static void *concurrent_claim(void *result)
{
    *(pet_growth_result_t *)result = pet_growth_interact(&concurrent_claim_request);
    return NULL;
}

static void check_daily_tasks(void)
{
    fresh();
    assert(act(PET_GROWTH_CLAIM_FEED, 1) == PET_GROWTH_TASK_INCOMPLETE);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(snapshot().task_progress[0] == 1);
    fail_key = "active";
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().coins == 25 && snapshot().task_claimed == 0);
    fail_key = NULL;
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_OK);
    assert(snapshot().coins == 30 && snapshot().exp == 23 && snapshot().task_claimed == 1);
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_DUPLICATE);
    assert(act(PET_GROWTH_CLAIM_FEED, 3) == PET_GROWTH_ALREADY_CLAIMED);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().task_claimed == 1 && snapshot().task_progress[0] == 1);
    assert(act(PET_GROWTH_CLAIM_FEED, 3) == PET_GROWTH_ALREADY_CLAIMED);
    for (unsigned i = 0; i < 3; ++i) {
        assert(act(PET_GROWTH_PET, 3 + i) == PET_GROWTH_OK);
        now_ms += 10000; now_epoch += 10;
    }
    assert(snapshot().task_progress[1] == 3);
    assert(act(PET_GROWTH_CLAIM_PET, 6) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_PLAY, 7) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_CLAIM_PLAY, 8) == PET_GROWTH_OK);
    assert(snapshot().task_claimed == 7 && snapshot().coins == 43 && snapshot().exp == 74);
    uint32_t old_day = snapshot().task_day;
    now_ms += 86400000; now_epoch += 86400;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().task_day > old_day && snapshot().task_claimed == 0);
    assert(snapshot().task_progress[0] == 0 && snapshot().task_progress[1] == 0);
    assert(act(PET_GROWTH_FEED, 9) == PET_GROWTH_OK);
    pet_growth_request_t yesterday = {10, now_ms, PET_GROWTH_CLAIM_FEED, old_day};
    assert(pet_growth_interact(&yesterday) == PET_GROWTH_STALE_REQUEST);
    assert(act(PET_GROWTH_CLAIM_FEED, 10) == PET_GROWTH_OK);
    now_epoch -= 86400;
    assert(pet_growth_tick() == PET_GROWTH_OK && snapshot().task_claimed == 1);

    fresh(); pet_growth_close();
    memcpy(blobs[0].data, growth_v1, sizeof(growth_v1)); blobs[0].size = sizeof(growth_v1);
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().coins == 20 && snapshot().task_progress[0] == 0 && snapshot().task_claimed == 0);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK && snapshot().task_progress[0] == 1);

    fresh(); pet_growth_close(); memset(blobs, 0, sizeof(blobs)); now_epoch = 0;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_TIME_UNAVAILABLE);
    now_epoch = 1800000000;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().task_progress[0] == 1);
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_OK);

    fresh();
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    concurrent_claim_request = (pet_growth_request_t){2, now_ms, PET_GROWTH_CLAIM_FEED, snapshot().task_day};
    pthread_t threads[2]; pet_growth_result_t results[2];
    for (unsigned i = 0; i < 2; ++i) assert(pthread_create(&threads[i], NULL, concurrent_claim, &results[i]) == 0);
    for (unsigned i = 0; i < 2; ++i) assert(pthread_join(threads[i], NULL) == 0);
    assert((results[0] == PET_GROWTH_OK && results[1] == PET_GROWTH_DUPLICATE) ||
           (results[1] == PET_GROWTH_OK && results[0] == PET_GROWTH_DUPLICATE));
    assert(snapshot().coins == 30 && snapshot().exp == 23);

    fresh(); pet_growth_close(); memset(blobs, 0, sizeof(blobs));
    now_epoch = 1727985599; /* UTC+8 本地 03:59:59 */
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    uint32_t before_day = snapshot().task_day;
    now_ms += 1000; now_epoch += 1;
    assert(pet_growth_tick() == PET_GROWTH_OK);
    assert(snapshot().task_day == before_day + 1 && snapshot().task_progress[0] == 0);
    assert(act(PET_GROWTH_CLAIM_FEED, 2) == PET_GROWTH_TASK_INCOMPLETE);

}

static void check_completion_rewards(void)
{
    fresh();
    uint32_t day=snapshot().task_day;
    assert(pet_growth_reward(PET_GROWTH_REWARD_DIALOGUE,100,day)==PET_GROWTH_OK);
    assert(snapshot().exp==2 && snapshot().intimacy==11);
    assert(pet_growth_reward(PET_GROWTH_REWARD_DIALOGUE,100,day)==PET_GROWTH_DUPLICATE);
    pet_growth_close(); assert(pet_growth_init(&clock_source)==PET_GROWTH_OK);
    assert(pet_growth_reward(PET_GROWTH_REWARD_DIALOGUE,100,day)==PET_GROWTH_DUPLICATE);
    for (unsigned i=101;i<150;++i) assert(pet_growth_reward(PET_GROWTH_REWARD_DIALOGUE,i,day)==PET_GROWTH_OK);
    uint32_t rev=snapshot().revision;
    assert(pet_growth_reward(PET_GROWTH_REWARD_DIALOGUE,150,day)==PET_GROWTH_ALREADY_CLAIMED);
    assert(snapshot().revision==rev);
    fail_key="active";
    assert(pet_growth_reward(PET_GROWTH_REWARD_FOCUS,200,day)==PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().revision==rev);
    fail_key=NULL;
    assert(pet_growth_reward(PET_GROWTH_REWARD_FOCUS,200,day)==PET_GROWTH_OK);
    assert(pet_growth_reward(PET_GROWTH_REWARD_FOCUS,200,day)==PET_GROWTH_DUPLICATE);
}

int main(void)
{
    check_backup_restore();
    check_first_care_achievement();
    check_achievement_thresholds();
    check_achievement_uncertain_commit();
    check_achievement_v2_migration();
    check_climate_exposure();
    check_climate_quality_and_expiry();
    check_climate_subsecond_and_restore();
    check_climate_storage_failures();
    fresh();
    assert(snapshot().coins == 20 && snapshot().revision == 1);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    assert(snapshot().coins == 25 && snapshot().exp == 8 && snapshot().revision == 2);
    pet_growth_close();
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().coins == 25 && snapshot().exp == 8);
    assert(act(PET_GROWTH_FEED, 1) == PET_GROWTH_DUPLICATE);
    unsigned before_events = events;
    fail_key = "active";
    assert(act(PET_GROWTH_PLAY, 2) == PET_GROWTH_STORAGE_ERROR);
    assert(snapshot().coins == 25 && snapshot().exp == 8 && snapshot().next_request_id == 2);
    assert(events == before_events);
    pet_growth_close();
    fail_key = NULL;
    assert(pet_growth_init(&clock_source) == PET_GROWTH_OK);
    assert(snapshot().coins == 25 && snapshot().next_request_id == 2);
    assert(act(PET_GROWTH_PLAY, 2) == PET_GROWTH_OK);
    assert(snapshot().coins == 28 && snapshot().exp == 23);
    check_time_and_recovery();
    check_storage_failures();
    check_concurrency();
    check_daily_tasks();
    check_level_up_transient();
    check_completion_rewards();
    pet_growth_close();
    puts("growth service: transactions, clocks, offline cap, versions, concurrency, achievements and environment passed");
    return 0;
}
