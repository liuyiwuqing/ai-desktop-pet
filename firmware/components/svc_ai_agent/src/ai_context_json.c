#include "svc_ai_agent/context.h"
#include <string.h>
#include "cJSON.h"

bool pet_ai_context_json(const pet_ai_context_t *c,const char *state,char *out,size_t cap,size_t *bytes)
{
    if (!c||!state||!out||!bytes||cap<2||strlen(state)>512) return false;
    *bytes=0;
    cJSON *root=cJSON_CreateObject();
    if (!root) return false;
    bool ok=cJSON_AddStringToObject(root,"persona","你是桌面宠物豆豆。简短、温和、好奇。不编造已执行动作；外部操作必须由用户确认。") &&
        cJSON_AddStringToObject(root,"state",state) && cJSON_AddStringToObject(root,"summary",c->summary);
    cJSON *history=cJSON_AddArrayToObject(root,"history");
    /* 最坏字符预算保守取字节数，历史不超过 2k，含人设/摘要/状态 <4k。 */
    size_t budget=0; unsigned first=c->count;
    while (first) {
        const pet_ai_context_turn_t *t=&c->turns[first-1];
        size_t n=strlen(t->user)+strlen(t->assistant)+32;
        if (budget+n>2000) break;
        budget+=n; --first;
    }
    for (unsigned i=first;ok && history && i<c->count;++i) {
        cJSON *item=cJSON_CreateObject();
        if (!item) { ok=false; break; }
        cJSON_AddItemToArray(history,item);
        ok=cJSON_AddNumberToObject(item,"turn",c->turns[i].turn) &&
            cJSON_AddStringToObject(item,"user",c->turns[i].user) &&
            cJSON_AddStringToObject(item,"assistant",c->turns[i].assistant);
    }
    ok=ok && history && cJSON_PrintPreallocated(root,out,(int)cap,false);
    if (ok) *bytes=strlen(out);
    cJSON_Delete(root); return ok;
}
