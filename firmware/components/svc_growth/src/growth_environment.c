#include "growth_environment.h"

#include <math.h>

static void account(pet_growth_environment_policy_t *p, uint64_t now)
{
    if (now < p->accounted_ms) return;
    uint64_t end = now < p->expires_ms ? now : p->expires_ms;
    if (p->effect != PET_GROWTH_ENV_NONE && end > p->accounted_ms) {
        uint64_t duration = end - p->accounted_ms + p->remainder_ms;
        p->due_s += (uint32_t)(duration / 1000);
        p->remainder_ms = (uint16_t)(duration % 1000);
    }
    p->accounted_ms = now;
}

pet_growth_result_t pet_growth_environment_observe(pet_growth_environment_policy_t *p,
    const pet_climate_reading_t *r, uint64_t now)
{
    uint32_t age = (uint32_t)now - r->timestamp_ms;
    if (age >= PET_GROWTH_ENV_FRESH_MS || now < p->accounted_ms) return PET_GROWTH_STALE_REQUEST;
    /* 过期后不再用旧采样序号比较，允许超过半个 32 位时钟周期的断流恢复。
     * 前面的新鲜度检查保证此时正常的旧读数仍会被拒绝。 */
    if (p->seen && now < p->expires_ms) {
        uint32_t difference = r->timestamp_ms - p->sample_ms;
        if (!difference) return PET_GROWTH_DUPLICATE;
        if (difference >= UINT32_C(0x80000000)) return PET_GROWTH_STALE_REQUEST;
    }
    account(p, now);
    p->effect = PET_GROWTH_ENV_NONE;
    if (r->valid && r->error_code == PET_CLIMATE_ERR_NONE && r->quality >= 50 && r->quality <= 100 &&
        isfinite(r->temperature_c) && isfinite(r->humidity_pct) &&
        r->temperature_c >= PET_CLIMATE_TEMP_MIN_C && r->temperature_c <= PET_CLIMATE_TEMP_MAX_C &&
        r->humidity_pct >= PET_CLIMATE_HUMIDITY_MIN_PCT && r->humidity_pct <= PET_CLIMATE_HUMIDITY_MAX_PCT) {
        /* 同时不适只扣一份心情；画面优先表达温度，再表达湿度。 */
        if (r->temperature_c < 10) p->effect = PET_GROWTH_ENV_COLD;
        else if (r->temperature_c > 30) p->effect = PET_GROWTH_ENV_HOT;
        else if (r->humidity_pct < 30) p->effect = PET_GROWTH_ENV_DRY;
        else if (r->humidity_pct > 80) p->effect = PET_GROWTH_ENV_HUMID;
    }
    p->sample_ms = r->timestamp_ms;
    p->seen = true;
    uint64_t remaining = PET_GROWTH_ENV_FRESH_MS - age;
    p->expires_ms = now > UINT64_MAX - remaining ? UINT64_MAX : now + remaining;
    return PET_GROWTH_OK;
}

uint32_t pet_growth_environment_consume(pet_growth_environment_policy_t *p, uint64_t now, uint32_t maximum)
{
    account(p, now);
    /* 子秒切换的已知暴露累加，不因每次更新取整而丢失；多出的不足一秒延后结算。 */
    uint32_t value = p->due_s < maximum ? p->due_s : maximum;
    p->due_s -= value;
    return value;
}

pet_growth_environment_t pet_growth_environment_current(const pet_growth_environment_policy_t *p, uint64_t now)
{
    return now >= p->accounted_ms && now < p->expires_ms ? p->effect : PET_GROWTH_ENV_NONE;
}
