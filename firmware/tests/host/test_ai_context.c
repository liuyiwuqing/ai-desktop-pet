#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "svc_ai_agent/context.h"

int main(void)
{
    pet_ai_context_t context={0};
    char json[PET_AI_CONTEXT_JSON_MAX]; size_t bytes;
    assert(pet_ai_context_add(&context,1,"你好","你好，我是豆豆"));
    assert(!pet_ai_context_add(&context,1,"重放","重复回复"));
    for (unsigned i=2;i<=13;++i) assert(pet_ai_context_add(&context,i,"今天怎样","今天也陪着你"));
    assert(context.count<=8 && context.summary[0]);
    assert(pet_ai_context_json(&context,"心情良好",json,sizeof(json),&bytes));
    assert(bytes==strlen(json) && strstr(json,"豆豆") && strstr(json,"summary"));
    assert(!pet_ai_context_json(&context,"",json,10,&bytes));
    pet_ai_context_clear(&context);
    assert(!context.count && !context.summary[0] && !context.last_turn);
    puts("AI context: bounded history, extractive summary, replay and privacy clear passed");
}
