#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cJSON.h"

#define PET_INTENT_TOOLS_MAX 16u
#define PET_INTENT_REPLY_MAX 8192u
typedef cJSON *(*pet_intent_execute_t)(const cJSON *arguments,void *context);
typedef struct {
    const char *name,*description,*input_schema;
    bool side_effect;
    pet_intent_execute_t execute;
    void *context;
} pet_intent_tool_t;
typedef struct {
    const pet_intent_tool_t *tools; size_t count;
    struct { uint32_t id,hash; char result[768]; } recent[8];
    unsigned next;
    uint32_t highest_id;
} pet_intent_rpc_t;
/* 单一网络拥有者调用；注册表生命周期覆盖服务，不执行任意文本或 shell。 */
bool pet_intent_init(pet_intent_rpc_t *service,const pet_intent_tool_t *tools,size_t count);
/* 新物理连接才能重置 request id；不能清除本轮的副作用去重。 */
void pet_intent_new_connection(pet_intent_rpc_t *service);
bool pet_intent_rpc(pet_intent_rpc_t *service,const char *request,size_t bytes,
                    char *out,size_t capacity,size_t *written);
