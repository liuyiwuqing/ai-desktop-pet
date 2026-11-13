#include "svc_ai_agent/context.h"
#include <string.h>

static size_t prefix(const char *text,size_t max)
{
    size_t n=strlen(text);
    if (n<=max) return n;
    n=max;
    while (n && ((unsigned char)text[n]&0xc0)==0x80) --n;
    return n;
}
void pet_ai_context_clear(pet_ai_context_t *c)
{
    if (!c) return;
    volatile unsigned char *p=(volatile unsigned char *)c;
    for (size_t i=0;i<sizeof(*c);++i) p[i]=0;
}
bool pet_ai_context_add(pet_ai_context_t *c,uint32_t turn,const char *user,const char *assistant)
{
    if (!c || !turn || !user || !assistant || turn<=c->last_turn) return false;
    if (c->count==PET_AI_CONTEXT_TURNS) {
        /* 每四轮摘录替换旧摘要；原文不落盘，最多保留八轮和 600 字节摘录。 */
        memset(c->summary,0,sizeof(c->summary));
        size_t used=0;
        for (unsigned i=0;i<4;++i) {
            const char *parts[]={"用户: ",c->turns[i].user," 回复: ",c->turns[i].assistant,"\n"};
            for (unsigned j=0;j<5;++j) {
                size_t n=prefix(parts[j],j==1||j==3?60:16);
                if (used+n>=sizeof(c->summary)) break;
                memcpy(c->summary+used,parts[j],n); used+=n;
            }
        }
        memmove(c->turns,c->turns+4,4*sizeof(c->turns[0]));
        memset(c->turns+4,0,4*sizeof(c->turns[0])); c->count=4;
    }
    pet_ai_context_turn_t *t=&c->turns[c->count++]; t->turn=turn;
    memcpy(t->user,user,prefix(user,PET_AI_TEXT_MAX));
    memcpy(t->assistant,assistant,prefix(assistant,PET_AI_TEXT_MAX));
    c->last_turn=turn; return true;
}
