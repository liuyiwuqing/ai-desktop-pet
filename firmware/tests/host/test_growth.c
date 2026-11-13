#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pet/growth.h"
#include "growth_engine.h"

static void check_daily_and_limits(void)
{
    pet_growth_model_t m;
    pet_growth_defaults(&m);
    pet_growth_calendar(&m, 1728014399, 0); /* 20000 日 03:59:59 */
    for (unsigned i = 1; i <= 21; ++i) {
        assert(pet_growth_apply(&m, PET_GROWTH_PET, i) == PET_GROWTH_OK);
        pet_growth_advance(&m, 10);
    }
    assert(m.exp == 41 && m.pet_count_today == 21);
    assert(m.intimacy == 30 * PET_GROWTH_UNITS + PET_GROWTH_UNITS / 2);
    pet_growth_calendar(&m, 1728014400, 0);
    assert(m.pet_count_today == 0);
    assert(pet_growth_apply(&m, PET_GROWTH_PET, 22) == PET_GROWTH_OK && m.exp == 43);
    pet_growth_calendar(&m, 1728014399, 0);
    assert(m.pet_count_today == 1); /* 回拨不重置 */

    pet_growth_defaults(&m);
    m.exp = 79;
    assert(pet_growth_apply(&m, PET_GROWTH_PLAY, 1) == PET_GROWTH_OK);
    assert(m.level == 2 && m.exp == 14);
    m.level = 98;
    m.exp = pet_growth_exp_to_next(98) - 1;
    assert(pet_growth_apply(&m, PET_GROWTH_FEED, 2) == PET_GROWTH_OK);
    assert(m.level == 99 && m.exp == 0);
    assert(pet_growth_apply(&m, PET_GROWTH_PET, 3) == PET_GROWTH_OK && m.exp == 0);

    pet_growth_defaults(&m);
    m.coins = 0;
    pet_growth_model_t before = m;
    assert(pet_growth_apply(&m, PET_GROWTH_FEED, 1) == PET_GROWTH_NO_COINS);
    assert(memcmp(&m, &before, sizeof(m)) == 0);
    m.energy = 19 * PET_GROWTH_UNITS;
    before = m;
    assert(pet_growth_apply(&m, PET_GROWTH_PLAY, 1) == PET_GROWTH_LOW_ENERGY);
    assert(memcmp(&m, &before, sizeof(m)) == 0);
    assert(pet_growth_apply(&m, PET_GROWTH_WAKE, 1) == PET_GROWTH_WRONG_STATE);
    assert(pet_growth_apply(&m, (pet_growth_action_t)-1, 1) == PET_GROWTH_INVALID);
    m.last_request_id = UINT64_MAX;
    assert(pet_growth_view(&m, 720, false).next_request_id == 0);
    assert(pet_growth_apply(&m, PET_GROWTH_FEED, 0) == PET_GROWTH_INVALID);
}

static void check_state_health_and_long_run(void)
{
    pet_growth_model_t m;
    pet_growth_defaults(&m);
    assert(pet_growth_view(&m, 22 * 60, false).state == PET_GROWTH_SLEEPY);
    assert(pet_growth_view(&m, 7 * 60, true).state == PET_GROWTH_EXCITED);
    m.mood = -31 * PET_GROWTH_UNITS;
    assert(pet_growth_view(&m, 720, false).state == PET_GROWTH_SAD);
    m.hunger = 0;
    assert(pet_growth_view(&m, 720, true).state == PET_GROWTH_HUNGRY);
    m.sleeping = true;
    assert(pet_growth_view(&m, 720, false).state == PET_GROWTH_SLEEPING);
    m.health = 29 * PET_GROWTH_UNITS;
    assert(pet_growth_view(&m, 720, false).state == PET_GROWTH_SICK);
    pet_growth_defaults(&m);
    m.hunger = m.energy = 0;
    pet_growth_advance(&m, 6 * 3600);
    assert(m.health == 100 * PET_GROWTH_UNITS);
    pet_growth_advance(&m, 3600);
    assert(m.health == 98 * PET_GROWTH_UNITS);
    m.hunger = m.energy = 100 * PET_GROWTH_UNITS;
    m.mood = 60 * PET_GROWTH_UNITS;
    pet_growth_advance(&m, 3600);
    assert(m.health == 99 * PET_GROWTH_UNITS);

    pet_growth_defaults(&m);
    for (unsigned h = 0; h < 30 * 24; ++h) {
        pet_growth_advance(&m, 3600);
        assert(pet_growth_model_valid(&m));
    }
    assert(m.hunger == 0 && m.energy == 0 && m.health == 0 && m.intimacy == 0);
    uint32_t random = 7;
    pet_growth_defaults(&m);
    for (unsigned i = 0; i < 1000; ++i) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        pet_growth_advance(&m, random % 3601);
        (void)pet_growth_apply(&m, (pet_growth_action_t)(random % PET_GROWTH_ACTION_COUNT), m.last_request_id + 1);
        assert(pet_growth_model_valid(&m));
    }
}

static void check_achievement_limits(void)
{
    pet_growth_model_t m;
    pet_growth_defaults(&m);
    assert(pet_growth_apply(&m, PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    assert(pet_growth_apply(&m, PET_GROWTH_PET, 2) == PET_GROWTH_ASLEEP);
    assert(m.achievements == 0 && m.coins == 20);
    assert(pet_growth_apply(&m, PET_GROWTH_WAKE, 2) == PET_GROWTH_OK && m.achievements == 0);
    m.coins = 999995;
    m.intimacy = 20 * PET_GROWTH_UNITS;
    m.level = 4; m.exp = 271;
    assert(pet_growth_apply(&m, PET_GROWTH_PET, 3) == PET_GROWTH_OK);
    assert(m.level == 5 && m.coins == 999999 && m.achievements == 7);
    assert(pet_growth_apply(&m, PET_GROWTH_PET, 3) == PET_GROWTH_DUPLICATE);
    assert(m.coins == 999999 && pet_growth_model_valid(&m));
    m.achievements |= 8;
    assert(!pet_growth_model_valid(&m));

    pet_growth_defaults(&m);
    assert(pet_growth_apply(&m, PET_GROWTH_PLAY, 1) == PET_GROWTH_OK);
    assert(m.coins == 33 && m.achievements == PET_GROWTH_ACHIEVEMENT_CARE);

    pet_growth_defaults(&m);
    m.level = 4; m.exp = 260;
    m.saved_epoch_s = 1800000000; m.daily_day = 20833;
    m.task_progress[0] = 1;
    assert(pet_growth_apply(&m, PET_GROWTH_CLAIM_FEED, 1) == PET_GROWTH_OK);
    assert(m.level == 5 && m.exp == 3 && m.coins == 55 && m.task_claimed == 1);
    assert(m.achievements == PET_GROWTH_ACHIEVEMENT_LEVEL);
}

int main(void)
{
    pet_growth_model_t model;
    pet_growth_defaults(&model);
    pet_growth_view_t view = pet_growth_view(&model, 12 * 60, false);
    assert(view.level == 1 && view.exp == 0 && view.coins == 20);
    assert(view.mood == 20 && view.energy == 80 && view.hunger == 70 && view.health == 100);
    assert(view.state == PET_GROWTH_IDLE);
    assert(pet_growth_apply(&model, PET_GROWTH_FEED, 1) == PET_GROWTH_OK);
    view = pet_growth_view(&model, 12 * 60, false);
    assert(view.hunger == 100 && view.coins == 25 && view.exp == 8 && view.intimacy == 12);
    assert(pet_growth_apply(&model, PET_GROWTH_FEED, 1) == PET_GROWTH_DUPLICATE);
    assert(pet_growth_apply(&model, PET_GROWTH_FEED, 2) == PET_GROWTH_COOLDOWN);
    assert(model.last_request_id == 1);
    pet_growth_advance(&model, 1800);
    view = pet_growth_view(&model, 12 * 60, false);
    assert(view.hunger == 98 && view.energy == 78 && view.feed_cooldown_s == 0);
    assert(pet_growth_apply(&model, PET_GROWTH_SLEEP, 2) == PET_GROWTH_OK);
    pet_growth_advance(&model, 3600);
    view = pet_growth_view(&model, 12 * 60, false);
    assert(view.hunger == 96 && view.energy == 86 && view.state == PET_GROWTH_SLEEPING);
    assert(pet_growth_apply(&model, PET_GROWTH_PLAY, 3) == PET_GROWTH_ASLEEP);
    assert(pet_growth_exp_to_next(20) == 3728 && pet_growth_exp_to_next(99) == 80832);

    /* 逐秒和整段结算一致；不足一分钟时切换睡眠也不能吞掉衰减。 */
    pet_growth_model_t stepped;
    pet_growth_defaults(&stepped);
    pet_growth_defaults(&model);
    for (unsigned i = 0; i < 3600; ++i)
        pet_growth_advance(&stepped, 1);
    pet_growth_advance(&model, 3600);
    assert(stepped.hunger == model.hunger && stepped.energy == model.energy);
    assert(stepped.mood == model.mood);
    pet_growth_defaults(&model);
    pet_growth_advance(&model, 30);
    assert(pet_growth_apply(&model, PET_GROWTH_SLEEP, 1) == PET_GROWTH_OK);
    pet_growth_advance(&model, 30);
    assert(model.hunger == 70 * PET_GROWTH_UNITS - 180);
    assert(model.energy == 80 * PET_GROWTH_UNITS + 150);

    check_daily_and_limits();
    check_state_health_and_long_run();
    check_achievement_limits();
    puts("growth: interactions, decay, daily reset, levels, states, 30 days and 1000 actions passed");
    return 0;
}
