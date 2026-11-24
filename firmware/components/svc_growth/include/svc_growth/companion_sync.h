#pragma once
#include <stddef.h>
#include "pet/companion.h"
#define PET_COMPANION_SYNC_MAX 3072u
#define PET_COMPANION_COMMAND_MAX 768u
int pet_companion_sync_format(const char *device,const char *boot,uint32_t sequence,uint32_t epoch,
    uint32_t revision,const pet_companion_view_t *view,char *out,size_t capacity);
pet_growth_result_t pet_companion_remote(const char *device,const char *json,size_t bytes,bool retained,
    uint32_t epoch,uint64_t monotonic_ms,char *reply,size_t capacity);
