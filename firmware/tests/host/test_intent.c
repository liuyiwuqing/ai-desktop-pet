#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "svc_intent/api.h"
static unsigned effects;
static const char *read_value="private-context";
static cJSON *read_tool(const cJSON *args,void *context)
{(void)args;(void)context;return cJSON_CreateString(read_value);}
static cJSON *execute(const cJSON *args,void *context)
{ (void)context; ++effects; return cJSON_Duplicate(args,true); }
int main(void)
{
    const pet_intent_tool_t tool={"self.test","test boundary","{\"type\":\"object\",\"properties\":{\"value\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3}},\"required\":[\"value\"],\"additionalProperties\":false}",true,execute,NULL};
    pet_intent_rpc_t service={0}; char out[8192]; size_t bytes;
    const pet_intent_tool_t tools[]={tool,{"self.read","read fresh state","{\"type\":\"object\",\"properties\":{}}",false,read_tool,NULL}};
    assert(pet_intent_init(&service,tools,2));
    const char request[]="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"self.test\",\"arguments\":{\"value\":2}}}";
    assert(pet_intent_rpc(&service,request,strlen(request),out,sizeof(out),&bytes));
    assert(effects==1 && strstr(out,"result"));
    assert(pet_intent_rpc(&service,request,strlen(request),out,sizeof(out),&bytes)&&effects==1);
    const char bad[]="{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":{\"name\":\"self.test\",\"arguments\":{\"value\":5}}}";
    assert(pet_intent_rpc(&service,bad,strlen(bad),out,sizeof(out),&bytes));
    assert(effects==1 && strstr(out,"-32602"));
    const char reused[]="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"self.test\",\"arguments\":{\"value\":3}}}";
    assert(pet_intent_rpc(&service,reused,strlen(reused),out,sizeof(out),&bytes));
    assert(effects==1 && strstr(out,"error"));
    const char initialized[]="{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}";
    assert(pet_intent_rpc(&service,initialized,strlen(initialized),out,sizeof(out),&bytes)&&bytes==0);
    const char deep[]="[[[[[[[[[0]]]]]]]]]";
    assert(!pet_intent_rpc(&service,deep,strlen(deep),out,sizeof(out),&bytes)&&effects==1);
    const char nul[]="{\"jsonrpc\\u0000bad\":\"2.0\",\"id\":4,\"method\":\"tools/list\"}";
    assert(!pet_intent_rpc(&service,nul,strlen(nul),out,sizeof(out),&bytes));
    const char duplicate[]="{\"jsonrpc\":\"2.0\",\"id\":3,\"id\":4,\"method\":\"tools/list\"}";
    assert(pet_intent_rpc(&service,duplicate,strlen(duplicate),out,sizeof(out),&bytes)&&strstr(out,"-32600"));
    const char read[]="{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"self.read\",\"arguments\":{}}}";
    assert(pet_intent_rpc(&service,read,strlen(read),out,sizeof(out),&bytes)&&strstr(out,"private-context"));
    read_value="cleared";
    assert(pet_intent_rpc(&service,read,strlen(read),out,sizeof(out),&bytes)&&strstr(out,"cleared")&&!strstr(out,"private-context"));
    puts("Intent: schema validation, fresh reads, results and duplicate effects passed");
}
