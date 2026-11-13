#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "pet/event.h"

typedef enum {
    PET_GROWTH_FEED = 0,
    PET_GROWTH_PET,
    PET_GROWTH_PLAY,
    PET_GROWTH_SLEEP,
    PET_GROWTH_WAKE,
    PET_GROWTH_CLAIM_FEED,
    PET_GROWTH_CLAIM_PET,
    PET_GROWTH_CLAIM_PLAY,
    PET_GROWTH_ACTION_COUNT,
} pet_growth_action_t;

enum {
    PET_GROWTH_ACHIEVEMENT_CARE = 1u << 0,
    PET_GROWTH_ACHIEVEMENT_FRIEND = 1u << 1,
    PET_GROWTH_ACHIEVEMENT_LEVEL = 1u << 2,
    PET_GROWTH_ACHIEVEMENT_ALL = 7,
    PET_GROWTH_CARE_COINS = 10,
    PET_GROWTH_FRIEND_COINS = 20,
    PET_GROWTH_LEVEL_COINS = 30,
    PET_GROWTH_FRIEND_TARGET = 20,
    PET_GROWTH_LEVEL_TARGET = 5,
};

typedef enum {
    PET_GROWTH_IDLE = 0,
    PET_GROWTH_HAPPY,
    PET_GROWTH_EXCITED,
    PET_GROWTH_SAD,
    PET_GROWTH_SLEEPY,
    PET_GROWTH_HUNGRY,
    PET_GROWTH_SICK,
    PET_GROWTH_SLEEPING,
} pet_growth_state_t;

typedef enum {
    PET_GROWTH_ENV_NONE = 0,
    PET_GROWTH_ENV_COLD,
    PET_GROWTH_ENV_HOT,
    PET_GROWTH_ENV_DRY,
    PET_GROWTH_ENV_HUMID,
} pet_growth_environment_t;

#define PET_GROWTH_ENV_FRESH_MS 30000u

typedef enum {
    PET_GROWTH_OK = 0,
    PET_GROWTH_DUPLICATE,
    PET_GROWTH_INVALID,
    PET_GROWTH_NOT_READY,
    PET_GROWTH_TIME_UNAVAILABLE,
    PET_GROWTH_STALE_REQUEST,
    PET_GROWTH_CONFLICT,
    PET_GROWTH_COOLDOWN,
    PET_GROWTH_NO_COINS,
    PET_GROWTH_LOW_ENERGY,
    PET_GROWTH_ASLEEP,
    PET_GROWTH_WRONG_STATE,
    PET_GROWTH_STORAGE_ERROR,
    PET_GROWTH_CORRUPT_SAVE,
    PET_GROWTH_UNKNOWN_VERSION,
    PET_GROWTH_STORAGE_UNCERTAIN,
    PET_GROWTH_TASK_INCOMPLETE,
    PET_GROWTH_ALREADY_CLAIMED,
} pet_growth_result_t;

/* 所有本地调用方共享递增 request_id；从快照取得 next_request_id，重试沿用原 ID。
 * 并发抢占返回 CONFLICT，先刷新快照；历史 ID 不会重复发放奖励。 */
typedef struct {
    uint64_t request_id;
    uint64_t issued_at_ms; /* 本次启动的单调时间，允许偏差不超过 5 分钟 */
    pet_growth_action_t action;
    uint32_t task_day; /* 领取时携带快照日期；跨日请求拒绝 */
} pet_growth_request_t;

typedef struct {
    uint64_t next_request_id;
    uint32_t exp;
    uint32_t exp_to_next;
    uint32_t coins;
    uint32_t revision;
    uint32_t task_day;
    uint8_t task_progress[3]; /* 喂食 1 / 摸头 3 / 玩耍 1，成功互动计数 */
    uint8_t task_claimed; /* bit 0..2：本日已领取 */
    uint8_t achievements; /* bit 0..2：一次性成就与金币奖励已共同提交 */
    uint16_t feed_cooldown_s;
    uint16_t play_cooldown_s;
    uint16_t pet_count_today;
    int16_t mood;
    uint8_t level;
    uint8_t intimacy;
    uint8_t energy;
    uint8_t hunger;
    uint8_t health;
    uint8_t pet_cooldown_s;
    pet_growth_state_t state;
    bool sleeping;
    bool time_ready;
    bool unsaved;
    bool storage_ready;
    bool save_recovered; /* 本次启动曾退回上一有效版本；可能丢失最后一次提交。 */
    uint8_t environment; /* pet_growth_environment_t；仅当前启动的可信新鲜读数，不持久化 */
} pet_growth_view_t;

_Static_assert(sizeof(pet_growth_view_t) <= PET_EVENT_PAYLOAD_MAX,
               "growth snapshot event must fit the event bus");

uint32_t pet_growth_exp_to_next(uint8_t level);
const char *pet_growth_result_name(pet_growth_result_t result);
const char *pet_growth_state_name(pet_growth_state_t state);
const char *pet_growth_environment_name(pet_growth_environment_t environment);
