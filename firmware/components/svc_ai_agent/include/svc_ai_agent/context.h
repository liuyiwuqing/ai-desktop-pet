#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "pet/ai.h"

#define PET_AI_CONTEXT_TURNS 8u
#define PET_AI_CONTEXT_JSON_MAX 8192u
typedef struct { uint32_t turn; char user[PET_AI_TEXT_MAX+1],assistant[PET_AI_TEXT_MAX+1]; } pet_ai_context_turn_t;
typedef struct {
    pet_ai_context_turn_t turns[PET_AI_CONTEXT_TURNS];
    char summary[601];
    uint32_t last_turn;
    uint8_t count;
} pet_ai_context_t;
/* 单一拥有者；只收已经实际完成播放的轮次。摘要为有界摘录，不伪称语义推理。 */
bool pet_ai_context_add(pet_ai_context_t *context,uint32_t turn,const char *user,const char *assistant);
void pet_ai_context_clear(pet_ai_context_t *context);
bool pet_ai_context_json(const pet_ai_context_t *context,const char *state,char *out,size_t capacity,size_t *bytes);
