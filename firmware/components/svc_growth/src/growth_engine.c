#include "growth_engine.h"

#include <limits.h>
#include <string.h>

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

uint32_t pet_growth_exp_to_next(uint8_t level)
{
    if (level < 1 || level > 99)
        return 0;
    const uint32_t n = level - 1;
    return 80 + 40 * n + 8 * n * n;
}

static void award_exp(pet_growth_model_t *model, uint32_t amount)
{
    model->exp += amount;
    while (model->level < 99 && model->exp >= pet_growth_exp_to_next(model->level)) {
        model->exp -= pet_growth_exp_to_next(model->level);
        ++model->level;
    }
    if (model->level == 99)
        model->exp = 0; /* 文档中的溢出转金币为可选项，首版不引入额外经济规则。 */
}

void pet_growth_defaults(pet_growth_model_t *model)
{
    memset(model, 0, sizeof(*model));
    model->level = 1;
    model->coins = 20;
    model->intimacy = 10 * PET_GROWTH_UNITS;
    model->mood = 20 * PET_GROWTH_UNITS;
    model->energy = 80 * PET_GROWTH_UNITS;
    model->hunger = 70 * PET_GROWTH_UNITS;
    model->health = PET_GROWTH_VALUE_MAX;
}

bool pet_growth_model_valid(const pet_growth_model_t *m)
{
    return m != NULL && m->level >= 1 && m->level <= 99 && m->coins <= 999999 &&
           m->exp < pet_growth_exp_to_next(m->level) && (m->level != 99 || m->exp == 0) &&
           m->intimacy <= PET_GROWTH_VALUE_MAX && m->energy <= PET_GROWTH_VALUE_MAX &&
           m->hunger <= PET_GROWTH_VALUE_MAX && m->health <= PET_GROWTH_VALUE_MAX &&
           m->mood >= -PET_GROWTH_VALUE_MAX && m->mood <= PET_GROWTH_VALUE_MAX &&
           m->feed_cooldown_s <= 1800 && m->play_cooldown_s <= 900 &&
           m->pet_cooldown_s <= 10 &&
           m->neglect_s <= 21600 && m->inactive_s < 86400 &&
           m->dialogues_today<=50 && m->focus_today<=8 &&
           m->last_action < PET_GROWTH_ACTION_COUNT && m->task_claimed <= 7 &&
           (m->achievements & ~PET_GROWTH_ACHIEVEMENT_ALL) == 0 &&
           m->task_progress[0] <= 1 && m->task_progress[1] <= 3 && m->task_progress[2] <= 1 &&
           (!(m->task_claimed & 1) || m->task_progress[0] == 1) &&
           (!(m->task_claimed & 2) || m->task_progress[1] == 3) &&
           (!(m->task_claimed & 4) || m->task_progress[2] == 1);
}

static void unlock_achievements(pet_growth_model_t *m, pet_growth_action_t action)
{
    unsigned eligible = action <= PET_GROWTH_PLAY ? PET_GROWTH_ACHIEVEMENT_CARE : 0;
    if (m->intimacy >= PET_GROWTH_FRIEND_TARGET * PET_GROWTH_UNITS)
        eligible |= PET_GROWTH_ACHIEVEMENT_FRIEND;
    if (m->level >= PET_GROWTH_LEVEL_TARGET) eligible |= PET_GROWTH_ACHIEVEMENT_LEVEL;
    const unsigned unlocked = eligible & ~m->achievements;
    unsigned reward = (unlocked & PET_GROWTH_ACHIEVEMENT_CARE ? PET_GROWTH_CARE_COINS : 0) +
                      (unlocked & PET_GROWTH_ACHIEVEMENT_FRIEND ? PET_GROWTH_FRIEND_COINS : 0) +
                      (unlocked & PET_GROWTH_ACHIEVEMENT_LEVEL ? PET_GROWTH_LEVEL_COINS : 0);
    m->achievements |= (uint8_t)unlocked;
    m->coins = (uint32_t)clamp((int)m->coins + (int)reward, 0, 999999);
}

pet_growth_result_t pet_growth_apply(pet_growth_model_t *m, pet_growth_action_t action,
                                     uint64_t request_id)
{
    if (!pet_growth_model_valid(m) || (unsigned)action >= PET_GROWTH_ACTION_COUNT || request_id == 0)
        return PET_GROWTH_INVALID;
    if (request_id <= m->last_request_id)
        return request_id == m->last_request_id && action != m->last_action
                   ? PET_GROWTH_CONFLICT : PET_GROWTH_DUPLICATE;
    if (m->last_request_id == UINT64_MAX || request_id != m->last_request_id + 1)
        return PET_GROWTH_CONFLICT;
    switch (action) {
    case PET_GROWTH_FEED:
        if (m->sleeping)
            return PET_GROWTH_ASLEEP;
        if (m->feed_cooldown_s != 0)
            return PET_GROWTH_COOLDOWN;
        if (m->coins < 5)
            return PET_GROWTH_NO_COINS;
        if (m->hunger > 90 * PET_GROWTH_UNITS)
            m->mood = (int32_t)clamp(m->mood - 5 * PET_GROWTH_UNITS, -PET_GROWTH_VALUE_MAX, PET_GROWTH_VALUE_MAX);
        m->hunger = (uint32_t)clamp(m->hunger + 30 * PET_GROWTH_UNITS, 0, PET_GROWTH_VALUE_MAX);
        m->intimacy = (uint32_t)clamp(m->intimacy + 2 * PET_GROWTH_UNITS, 0, PET_GROWTH_VALUE_MAX);
        m->coins -= 5;
        award_exp(m, 8);
        m->feed_cooldown_s = 1800;
        break;
    case PET_GROWTH_PET: {
        if (m->sleeping)
            return PET_GROWTH_ASLEEP;
        if (m->pet_cooldown_s != 0)
            return PET_GROWTH_COOLDOWN;
        const int unit = m->pet_count_today < 20 ? PET_GROWTH_UNITS : PET_GROWTH_UNITS / 2;
        m->intimacy = (uint32_t)clamp(m->intimacy + unit, 0, PET_GROWTH_VALUE_MAX);
        m->mood = (int32_t)clamp(m->mood + 3 * unit, -PET_GROWTH_VALUE_MAX, PET_GROWTH_VALUE_MAX);
        award_exp(m, unit == PET_GROWTH_UNITS ? 2 : 1);
        if (m->pet_count_today < UINT16_MAX)
            ++m->pet_count_today;
        m->pet_cooldown_s = 10;
        break;
    }
    case PET_GROWTH_PLAY:
        if (m->sleeping)
            return PET_GROWTH_ASLEEP;
        if (m->play_cooldown_s != 0)
            return PET_GROWTH_COOLDOWN;
        if (m->energy < 20 * PET_GROWTH_UNITS)
            return PET_GROWTH_LOW_ENERGY;
        m->mood = (int32_t)clamp(m->mood + 10 * PET_GROWTH_UNITS, -PET_GROWTH_VALUE_MAX, PET_GROWTH_VALUE_MAX);
        m->energy -= 10 * PET_GROWTH_UNITS;
        m->coins = (uint32_t)clamp((int)m->coins + 3, 0, 999999);
        award_exp(m, 15);
        m->play_cooldown_s = 900;
        break;
    case PET_GROWTH_SLEEP:
        if (m->sleeping)
            return PET_GROWTH_WRONG_STATE;
        m->sleeping = true;
        break;
    case PET_GROWTH_WAKE:
        if (!m->sleeping)
            return PET_GROWTH_WRONG_STATE;
        m->sleeping = false;
        break;
    case PET_GROWTH_CLAIM_FEED:
    case PET_GROWTH_CLAIM_PET:
    case PET_GROWTH_CLAIM_PLAY: {
        if (!m->daily_day || !m->saved_epoch_s) return PET_GROWTH_TIME_UNAVAILABLE;
        const unsigned task = action - PET_GROWTH_CLAIM_FEED;
        const unsigned target = task == 1 ? 3 : 1;
        if (m->task_claimed & (1u << task)) return PET_GROWTH_ALREADY_CLAIMED;
        if (m->task_progress[task] < target) return PET_GROWTH_TASK_INCOMPLETE;
        m->task_claimed |= (uint8_t)(1u << task);
        m->coins = (uint32_t)clamp((int)m->coins + 5, 0, 999999);
        award_exp(m, 15);
        break;
    }
    default:
        return PET_GROWTH_INVALID;
    }
    if (action <= PET_GROWTH_PLAY) {
        const unsigned target = action == PET_GROWTH_PET ? 3 : 1;
        if (m->task_progress[action] < target) ++m->task_progress[action];
    }
    /* 仅成功操作结算；旧档不据请求编号推断历史照料，不在读取/衰减时发奖。 */
    unlock_achievements(m, action);
    m->last_request_id = request_id;
    m->last_action = (uint8_t)action;
    if (action < PET_GROWTH_CLAIM_FEED) m->inactive_s = 0;
    return PET_GROWTH_OK;
}

void pet_growth_advance(pet_growth_model_t *m, uint32_t elapsed_s)
{
    pet_growth_advance_environment(m, elapsed_s, 0);
}

void pet_growth_advance_environment(pet_growth_model_t *m, uint32_t elapsed_s, uint32_t exposed_s)
{
    /* 调用方将长时间间隔分批；离线单次最多八小时，循环有明确上界。 */
    if (!pet_growth_model_valid(m) || elapsed_s > PET_GROWTH_OFFLINE_MAX_S || exposed_s > elapsed_s)
        return;
    for (uint32_t i = 0; i < elapsed_s; ++i) {
        if (m->feed_cooldown_s > 0) --m->feed_cooldown_s;
        if (m->pet_cooldown_s > 0) --m->pet_cooldown_s;
        if (m->play_cooldown_s > 0) --m->play_cooldown_s;
        m->hunger = (uint32_t)clamp((int)m->hunger - (m->sleeping ? 2 : 4), 0, PET_GROWTH_VALUE_MAX);
        m->energy = (uint32_t)clamp((int)m->energy + (m->sleeping ? 8 : -3), 0, PET_GROWTH_VALUE_MAX);
        if (!m->sleeping) {
            if (m->mood > 0) --m->mood;
            else if (m->mood < 0) ++m->mood;
        }
        if (m->hunger < 20 * PET_GROWTH_UNITS || m->energy < 20 * PET_GROWTH_UNITS)
            m->mood = clamp(m->mood - 1, -PET_GROWTH_VALUE_MAX, PET_GROWTH_VALUE_MAX);
        if (i < exposed_s) m->mood = clamp(m->mood - 1, -PET_GROWTH_VALUE_MAX, PET_GROWTH_VALUE_MAX);
        if (m->hunger < 10 * PET_GROWTH_UNITS || m->energy < 10 * PET_GROWTH_UNITS) {
            if (m->neglect_s < 21600) ++m->neglect_s;
            else m->health = (uint32_t)clamp((int)m->health - 2, 0, PET_GROWTH_VALUE_MAX);
        } else {
            m->neglect_s = 0;
            if (m->hunger > 50 * PET_GROWTH_UNITS && m->energy > 50 * PET_GROWTH_UNITS &&
                m->mood > 50 * PET_GROWTH_UNITS)
                m->health = (uint32_t)clamp((int)m->health + 1, 0, PET_GROWTH_VALUE_MAX);
        }
        if (++m->inactive_s == 86400) {
            m->inactive_s = 0;
            m->intimacy = (uint32_t)clamp((int)m->intimacy - PET_GROWTH_UNITS, 0, PET_GROWTH_VALUE_MAX);
        }
    }
}

void pet_growth_calendar(pet_growth_model_t *m, uint32_t epoch_s, int16_t utc_offset_min)
{
    if (epoch_s == 0 || utc_offset_min < -720 || utc_offset_min > 840)
        return;
    const int64_t day = ((int64_t)epoch_s + (int64_t)utc_offset_min * 60 - 4 * 3600) / 86400;
    if (day <= 0)
        return;
    if ((uint32_t)day > m->daily_day) {
        /* 未对时时累积的次数保留到下一真实日，不因首次对时白送一次重置。 */
        if (m->daily_day != 0) {
            m->pet_count_today = 0;
            m->dialogues_today=m->focus_today=0;
            memset(m->task_progress, 0, sizeof(m->task_progress));
            m->task_claimed = 0;
        }
        m->daily_day = (uint32_t)day;
    }
}

pet_growth_view_t pet_growth_view(const pet_growth_model_t *m, uint16_t local_minute, bool excited)
{
    pet_growth_view_t view = {
        .next_request_id = m->last_request_id == UINT64_MAX ? 0 : m->last_request_id + 1,
        .level = m->level, .exp = m->exp, .exp_to_next = pet_growth_exp_to_next(m->level),
        .coins = m->coins, .intimacy = m->intimacy / PET_GROWTH_UNITS,
        .mood = m->mood / PET_GROWTH_UNITS, .energy = m->energy / PET_GROWTH_UNITS,
        .hunger = m->hunger / PET_GROWTH_UNITS, .health = m->health / PET_GROWTH_UNITS,
        .sleeping = m->sleeping, .feed_cooldown_s = m->feed_cooldown_s,
        .pet_cooldown_s = m->pet_cooldown_s, .play_cooldown_s = m->play_cooldown_s,
        .pet_count_today = m->pet_count_today,
        .task_day = m->daily_day, .task_claimed = m->task_claimed,
        .achievements = m->achievements,
        .task_progress = {m->task_progress[0], m->task_progress[1], m->task_progress[2]},
    };
    if (m->health < 30 * PET_GROWTH_UNITS) view.state = PET_GROWTH_SICK;
    else if (m->sleeping) view.state = PET_GROWTH_SLEEPING;
    else if (m->hunger < 20 * PET_GROWTH_UNITS) view.state = PET_GROWTH_HUNGRY;
    else if (m->energy < 20 * PET_GROWTH_UNITS || (local_minute < 1440 &&
             (local_minute >= 1320 || local_minute < 420))) view.state = PET_GROWTH_SLEEPY;
    else if (m->mood < -30 * PET_GROWTH_UNITS) view.state = PET_GROWTH_SAD;
    else if (excited) view.state = PET_GROWTH_EXCITED;
    else if (m->mood > 50 * PET_GROWTH_UNITS && m->energy > 50 * PET_GROWTH_UNITS &&
             m->hunger > 50 * PET_GROWTH_UNITS && m->health > 50 * PET_GROWTH_UNITS)
        view.state = PET_GROWTH_HAPPY;
    else view.state = PET_GROWTH_IDLE;
    return view;
}

const char *pet_growth_state_name(pet_growth_state_t state)
{
    static const char *names[] = {"idle", "happy", "excited", "sad", "sleepy", "hungry", "sick", "sleeping"};
    return (unsigned)state < sizeof(names) / sizeof(names[0]) ? names[state] : "unknown";
}

const char *pet_growth_environment_name(pet_growth_environment_t environment)
{
    static const char *names[] = {"none", "cold", "hot", "dry", "humid"};
    return (unsigned)environment < sizeof(names) / sizeof(names[0]) ? names[environment] : "unknown";
}

const char *pet_growth_result_name(pet_growth_result_t result)
{
    static const char *names[] = {"ok", "duplicate", "invalid", "not_ready", "time_unavailable",
        "stale_request", "conflict", "cooldown", "no_coins", "low_energy", "asleep", "wrong_state",
        "storage_error", "corrupt_save", "unknown_version", "storage_uncertain", "task_incomplete", "already_claimed"};
    return (unsigned)result < sizeof(names) / sizeof(names[0]) ? names[result] : "unknown";
}

pet_growth_result_t pet_growth_award_completion(pet_growth_model_t *m,bool focus,uint64_t event_id)
{
    if (!m || !event_id || !pet_growth_model_valid(m)) return PET_GROWTH_INVALID;
    uint64_t *last=focus?&m->last_focus_event:&m->last_dialogue_event;
    if (event_id<=*last) return PET_GROWTH_DUPLICATE;
    uint8_t *count=focus?&m->focus_today:&m->dialogues_today;
    if (*count>=(focus?8:50)) return PET_GROWTH_ALREADY_CLAIMED;
    if (!m->daily_day) return PET_GROWTH_TIME_UNAVAILABLE;
    ++*count; *last=event_id;
    award_exp(m,focus?20:2);
    if (focus) m->coins=(uint32_t)clamp((int)m->coins+10,0,999999);
    else m->intimacy=(uint32_t)clamp((int)m->intimacy+PET_GROWTH_UNITS,0,PET_GROWTH_VALUE_MAX);
    unlock_achievements(m,PET_GROWTH_WAKE);
    return PET_GROWTH_OK;
}
