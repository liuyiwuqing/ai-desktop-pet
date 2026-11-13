#include "svc_intent/api.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "pet/json_guard.h"

static const cJSON *get(const cJSON *r,const char *key) { return cJSON_GetObjectItemCaseSensitive(r,key); }
static bool is(const cJSON *r,const char *value) { return cJSON_IsString(r)&&!strcmp(r->valuestring,value); }
static bool unique(const cJSON *r,unsigned depth)
{
    if (depth>8) return false;
    for (const cJSON *a=r?r->child:NULL;a;a=a->next) {
        if (cJSON_IsObject(r)) for (const cJSON *b=a->next;b;b=b->next)
            if (!strcmp(a->string,b->string)) return false;
        if (!unique(a,depth+1)) return false;
    }
    return true;
}
/* 本项目工具使用的有界 JSON Schema 子集。未识别类型失败，不隐式转换参数。 */
static bool validate(const cJSON *schema,const cJSON *value,unsigned depth)
{
    if (depth>4||!schema||!value) return false;
    const cJSON *type=get(schema,"type");
    if (is(type,"object")) {
        if (!cJSON_IsObject(value)) return false;
        const cJSON *properties=get(schema,"properties"),*required=get(schema,"required");
        for (const cJSON *p=value->child;p;p=p->next) {
            const cJSON *rule=get(properties,p->string);
            if (!rule||!validate(rule,p,depth+1)) return false;
        }
        for (const cJSON *p=required?required->child:NULL;p;p=p->next)
            if (!cJSON_IsString(p)||!get(value,p->valuestring)) return false;
        return true;
    }
    if (is(type,"integer")||is(type,"number")) {
        if (!cJSON_IsNumber(value)||!isfinite(value->valuedouble)||
            (is(type,"integer")&&floor(value->valuedouble)!=value->valuedouble)) return false;
        const cJSON *min=get(schema,"minimum"),*max=get(schema,"maximum");
        return (!min||value->valuedouble>=min->valuedouble)&&(!max||value->valuedouble<=max->valuedouble);
    }
    if (is(type,"boolean")) return cJSON_IsBool(value);
    if (is(type,"string")) {
        if (!cJSON_IsString(value)) return false;
        size_t n=strlen(value->valuestring);
        if (n>256) return false;
        const cJSON *max=get(schema,"maxLength"),*options=get(schema,"enum");
        if (max&&n>max->valuedouble) return false;
        for (size_t i=0;i<n;++i) if ((unsigned char)value->valuestring[i]<32) return false;
        if (options) {
            for (const cJSON *p=options->child;p;p=p->next) if (is(p,value->valuestring)) return true;
            return false;
        }
        return true;
    }
    return false;
}
bool pet_intent_init(pet_intent_rpc_t *s,const pet_intent_tool_t *tools,size_t count)
{
    if (!s||!tools||!count||count>PET_INTENT_TOOLS_MAX) return false;
    for (size_t i=0;i<count;++i) {
        if (!tools[i].name||!tools[i].description||!tools[i].execute||!tools[i].input_schema) return false;
        cJSON *schema=cJSON_Parse(tools[i].input_schema);
        bool ok=cJSON_IsObject(schema)&&is(get(schema,"type"),"object"); cJSON_Delete(schema);
        if (!ok) return false;
        for (size_t j=0;j<i;++j) if (!strcmp(tools[i].name,tools[j].name)) return false;
    }
    memset(s,0,sizeof(*s)); s->tools=tools; s->count=count; return true;
}
void pet_intent_new_connection(pet_intent_rpc_t *s)
{ if (s) { memset(s->recent,0,sizeof(s->recent)); s->next=s->highest_id=0; } }
static cJSON *error(int code,const char *message)
{
    cJSON *r=cJSON_CreateObject(),*e=cJSON_AddObjectToObject(r,"error");
    cJSON_AddNumberToObject(e,"code",code); cJSON_AddStringToObject(e,"message",message); return r;
}
static cJSON *content(cJSON *value)
{
    char *text=cJSON_PrintUnformatted(value); cJSON_Delete(value);
    cJSON *r=cJSON_CreateObject(),*result=cJSON_AddObjectToObject(r,"result");
    cJSON *array=cJSON_AddArrayToObject(result,"content"),*item=cJSON_CreateObject();
    if (!r||!result||!array||!item||!text) { cJSON_Delete(r);cJSON_Delete(item);cJSON_free(text);return NULL; }
    cJSON_AddItemToArray(array,item); cJSON_AddStringToObject(item,"type","text");
    cJSON_AddStringToObject(item,"text",text); cJSON_AddBoolToObject(result,"isError",false);
    cJSON_free(text); return r;
}
bool pet_intent_rpc(pet_intent_rpc_t *s,const char *request,size_t bytes,char *out,size_t cap,size_t *written)
{
    if (!s||!s->count||!request||!bytes||bytes>1024||!pet_json_bounded(request,bytes,8)||!out||!written||cap<128) return false;
    *written=0;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(request,bytes,&end,false),*reply=NULL;
    while (root && end<request+bytes && (*end==' '||*end=='\n'||*end=='\t'||*end=='\r')) ++end;
    uint32_t id=0,hash=2166136261u;
    for (size_t i=0;i<bytes;++i) hash=(hash^(unsigned char)request[i])*16777619u;
    const cJSON *identifier=get(root,"id"),*method=get(root,"method"),*params=get(root,"params");
    if (root && end==request+bytes && unique(root,0) && is(get(root,"jsonrpc"),"2.0") &&
        is(method,"notifications/initialized") && !identifier) {
        cJSON_Delete(root); out[0]=0; return true;
    }
    if (!cJSON_IsObject(root)||end!=request+bytes||!unique(root,0)||!is(get(root,"jsonrpc"),"2.0")||
        !cJSON_IsNumber(identifier)||identifier->valuedouble<1||identifier->valuedouble>UINT32_MAX||
        floor(identifier->valuedouble)!=identifier->valuedouble||!cJSON_IsString(method)) {
        reply=error(-32600,"Invalid request"); goto encode;
    }
    id=(uint32_t)identifier->valuedouble;
    if (is(method,"initialize")) {
        reply=cJSON_Parse("{\"result\":{\"protocolVersion\":\"2024-11-05\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"dodo\",\"version\":\"1\"}}}");
    } else if (is(method,"tools/list")) {
        reply=cJSON_CreateObject(); cJSON *result=cJSON_AddObjectToObject(reply,"result");
        cJSON *list=cJSON_AddArrayToObject(result,"tools");
        for (size_t i=0;i<s->count;++i) {
            cJSON *tool=cJSON_CreateObject(); cJSON_AddItemToArray(list,tool);
            cJSON_AddStringToObject(tool,"name",s->tools[i].name);
            cJSON_AddStringToObject(tool,"description",s->tools[i].description);
            cJSON_AddItemToObject(tool,"inputSchema",cJSON_Parse(s->tools[i].input_schema));
        }
    } else if (is(method,"tools/call")) {
        const cJSON *name=get(params,"name"),*args=get(params,"arguments");
        size_t index=0;
        while (index<s->count&&!is(name,s->tools[index].name)) ++index;
        if (!cJSON_IsObject(params)||index==s->count) { reply=error(-32602,"Unknown tool"); goto encode; }
        const pet_intent_tool_t *tool=&s->tools[index];
        for (unsigned i=0;i<8;++i) if (s->recent[i].id==id) {
            if (s->recent[i].hash!=hash) reply=error(-32600,"Request id reused");
            else if (!tool->side_effect) reply=content(tool->execute(args,tool->context));
            else if (s->recent[i].result[0]) reply=cJSON_Parse(s->recent[i].result);
            else reply=error(-32001,"Result no longer retained; action was not repeated");
            goto encode;
        }
        if (tool->side_effect && id<=s->highest_id) { reply=error(-32001,"Stale request id"); goto encode; }
        cJSON *schema=cJSON_Parse(tool->input_schema);
        bool ok=validate(schema,args,0); cJSON_Delete(schema);
        if (!ok) { reply=error(-32602,"Invalid tool arguments"); goto encode; }
        /* 预留去重位置后再执行；失败同样不重放。超出结果缓存时仍保留已执行标记。 */
        unsigned slot=s->next++%8;
        memset(&s->recent[slot],0,sizeof(s->recent[slot]));
        s->recent[slot].id=id; s->recent[slot].hash=hash;
        if (id>s->highest_id) s->highest_id=id;
        cJSON *value=tool->execute(args,tool->context);
        reply=value ? content(value) : error(-32000,"Tool unavailable or operation failed");
        /* 只缓存副作用结果；只读上下文重投重新读取，避免隐私清除后重发旧文本。 */
        if (tool->side_effect && reply && !cJSON_PrintPreallocated(reply,s->recent[slot].result,sizeof(s->recent[slot].result),false))
            s->recent[slot].result[0]=0;
    } else reply=error(-32601,"Method not found");
encode:
    cJSON_Delete(root);
    if (!reply) return false;
    cJSON_AddStringToObject(reply,"jsonrpc","2.0");
    if (id) cJSON_AddNumberToObject(reply,"id",id); else cJSON_AddNullToObject(reply,"id");
    bool ok=cJSON_PrintPreallocated(reply,out,(int)cap,false);
    if (ok) *written=strlen(out);
    cJSON_Delete(reply); return ok;
}
