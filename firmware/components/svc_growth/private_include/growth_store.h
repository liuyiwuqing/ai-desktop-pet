#pragma once

#include "growth_engine.h"

typedef struct {
    uint32_t revision;
    int8_t active_slot; /* -1 为全新存档；恢复回退后指向实际有效槽 */
    bool write_uncertain;
    bool recovered;
} pet_growth_store_t;

pet_growth_result_t pet_growth_store_load(pet_growth_store_t *store, pet_growth_model_t *model);
pet_growth_result_t pet_growth_store_save(pet_growth_store_t *store, const pet_growth_model_t *model);
pet_growth_result_t pet_growth_backup_encode(const pet_growth_model_t *model,uint32_t revision,
    void *out,size_t capacity,size_t *bytes);
pet_growth_result_t pet_growth_backup_decode(const void *data,size_t bytes,pet_growth_model_t *out);
