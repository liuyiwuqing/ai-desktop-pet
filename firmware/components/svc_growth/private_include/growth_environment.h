#pragma once

#include "pet/climate.h"
#include "pet/growth.h"

/* 新鲜度和已知暴露只存在 RAM；不把最后一次读数当作离线期间的环境。 */
typedef struct {
    uint64_t accounted_ms, expires_ms;
    uint32_t sample_ms, due_s;
    uint16_t remainder_ms;
    pet_growth_environment_t effect;
    bool seen;
} pet_growth_environment_policy_t;

pet_growth_result_t pet_growth_environment_observe(pet_growth_environment_policy_t *policy,
    const pet_climate_reading_t *reading, uint64_t now_ms);
uint32_t pet_growth_environment_consume(pet_growth_environment_policy_t *policy,
                                       uint64_t now_ms, uint32_t maximum_s);
pet_growth_environment_t pet_growth_environment_current(const pet_growth_environment_policy_t *policy,
                                                        uint64_t now_ms);
