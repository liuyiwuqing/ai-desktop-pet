#include "app_tools.h"
#include "app_behavior.h"
#include "sdkconfig.h"
#if CONFIG_PET_AI_XIAOZHI_ON_BOOT
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "svc_ai_agent/wss.h"
#include "svc_intent/api.h"
#include "svc_focus/api.h"
#if CONFIG_PET_MODULE_GROWTH_ENABLED
#include "svc_growth/api.h"
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
#include "svc_climate/api.h"
#endif
#include "svc_message_hub/api.h"
#if CONFIG_PET_EXT_DEVICE_CONTROL
#include "pet_ext/control_service.h"
#endif
#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo_service.h"
#endif

static pet_intent_rpc_t rpc;
static uint32_t connection_epoch;
static cJSON *state(const cJSON *args,void *context)
{
    (void)args;(void)context;
    cJSON *out=cJSON_CreateObject();
    if (!out) return NULL;
    cJSON_AddNumberToObject(out,"unread",pet_message_hub_depth());
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    pet_growth_view_t growth;
    if (pet_growth_get_snapshot(&growth)==PET_GROWTH_OK) {
        cJSON_AddStringToObject(out,"mood",pet_growth_state_name(growth.state));
        cJSON_AddNumberToObject(out,"hunger",growth.hunger);
        cJSON_AddNumberToObject(out,"energy",growth.energy);
        cJSON_AddNumberToObject(out,"level",growth.level);
        cJSON_AddNumberToObject(out,"nextRequestId",(double)growth.next_request_id);
    }
#endif
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    pet_climate_snapshot_t climate;
    if (pet_climate_get_snapshot(&climate)==ESP_OK && climate.reading.valid && climate.age_ms<30000) {
        cJSON_AddNumberToObject(out,"temperatureC",climate.reading.temperature_c);
        cJSON_AddNumberToObject(out,"humidityPct",climate.reading.humidity_pct);
    }
#endif
    return out;
}
static cJSON *context_tool(const cJSON *args,void *context)
{
    (void)args;(void)context;
    pet_ai_context_t *history=heap_caps_malloc(sizeof(*history),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    char *json=heap_caps_malloc(PET_AI_CONTEXT_JSON_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    cJSON *result=NULL,*snapshot=state(NULL,NULL);
    char *status=cJSON_PrintUnformatted(snapshot);cJSON_Delete(snapshot);
    size_t bytes=0;
    if (history&&json&&status&&pet_ai_agent_context(history)==ESP_OK &&
        pet_ai_context_json(history,status,json,PET_AI_CONTEXT_JSON_MAX,&bytes)) result=cJSON_Parse(json);
    if (history) pet_ai_context_clear(history);
    if (json) memset(json,0,PET_AI_CONTEXT_JSON_MAX);
    heap_caps_free(history);heap_caps_free(json);cJSON_free(status);return result;
}
static cJSON *unread(const cJSON *args,void *context)
{
    (void)args;(void)context;
    cJSON *out=cJSON_CreateObject();cJSON_AddNumberToObject(out,"count",pet_message_hub_depth());return out;
}
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
static cJSON *climate_tool(const cJSON *args,void *context)
{
    (void)args;(void)context;
    pet_climate_snapshot_t snapshot;
    if (pet_climate_get_snapshot(&snapshot)!=ESP_OK) return NULL;
    cJSON *out=cJSON_CreateObject();
    cJSON_AddBoolToObject(out,"valid",snapshot.reading.valid&&snapshot.age_ms<30000);
    cJSON_AddNumberToObject(out,"ageMs",snapshot.age_ms);
    cJSON_AddNumberToObject(out,"status",snapshot.status);
    if (snapshot.reading.valid) {
        cJSON_AddNumberToObject(out,"temperatureC",snapshot.reading.temperature_c);
        cJSON_AddNumberToObject(out,"humidityPct",snapshot.reading.humidity_pct);
    }
    return out;
}
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
static cJSON *interact(const cJSON *args,void *context)
{
    (void)context;
    const char *action=cJSON_GetObjectItemCaseSensitive(args,"action")->valuestring;
    pet_growth_action_t kind=!strcmp(action,"feed")?PET_GROWTH_FEED:!strcmp(action,"pet")?PET_GROWTH_PET:
        !strcmp(action,"play")?PET_GROWTH_PLAY:!strcmp(action,"sleep")?PET_GROWTH_SLEEP:PET_GROWTH_WAKE;
    pet_growth_view_t snapshot;
    if (pet_growth_get_snapshot(&snapshot)!=PET_GROWTH_OK) return NULL;
    pet_growth_request_t request={.request_id=(uint64_t)cJSON_GetObjectItemCaseSensitive(args,"requestId")->valuedouble,.task_day=snapshot.task_day,
        .action=kind,.issued_at_ms=(uint64_t)esp_timer_get_time()/1000};
    pet_growth_result_t result=pet_growth_interact(&request);
    cJSON *out=cJSON_CreateObject();
    cJSON_AddBoolToObject(out,"ok",result==PET_GROWTH_OK);
    cJSON_AddStringToObject(out,"result",pet_growth_result_name(result));return out;
}
#endif
#if CONFIG_PET_EXT_SERVO
static cJSON *servo_tool(const cJSON *args,void *context)
{
    (void)context;
    const char *action=cJSON_GetObjectItemCaseSensitive(args,"action")->valuestring;
    esp_err_t error=ESP_OK;
    if(!strcmp(action,"stop")||!strcmp(action,"emergency"))error=pet_servo_service_stop(!strcmp(action,"emergency"));
    else if(strcmp(action,"status")) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(args,"requestId"),*angle=cJSON_GetObjectItemCaseSensitive(args,"angle"),
            *duration=cJSON_GetObjectItemCaseSensitive(args,"durationMs");
        if(!cJSON_IsNumber(id)||(!strcmp(action,"move")&&(!angle||!duration)))error=ESP_ERR_INVALID_ARG;
        else error=pet_servo_service_submit((uint64_t)id->valuedouble,
            !strcmp(action,"home")?PET_SERVO_CMD_HOME:!strcmp(action,"wag")?PET_SERVO_CMD_WAG:!strcmp(action,"nod")?PET_SERVO_CMD_NOD:PET_SERVO_CMD_MOVE,
            angle?(int16_t)angle->valuedouble:0,duration?(uint16_t)duration->valuedouble:1000);
    }
    pet_servo_service_view_t view={0};esp_err_t snapshot_error=pet_servo_service_snapshot(&view);
    cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"accepted",error==ESP_OK&&snapshot_error==ESP_OK);
    cJSON_AddBoolToObject(out,"configured",view.configured);cJSON_AddBoolToObject(out,"armed",view.state.armed);
    cJSON_AddBoolToObject(out,"moving",view.state.moving);cJSON_AddBoolToObject(out,"emergency",view.state.emergency);
    cJSON_AddBoolToObject(out,"fault",view.state.fault);cJSON_AddNumberToObject(out,"angleCommanded",view.state.angle);
    cJSON_AddNumberToObject(out,"requestId",(double)view.request_id);cJSON_AddNumberToObject(out,"result",view.result);
    cJSON_AddNumberToObject(out,"nextRequestId",(double)(view.state.last_request_id+1));return out;
}
#endif
#if CONFIG_PET_EXT_DEVICE_CONTROL
static cJSON *device_tool(const cJSON *args,void *context)
{
    (void)context;
    const char *action=cJSON_GetObjectItemCaseSensitive(args,"action")->valuestring;
    esp_err_t error=ESP_OK;
    if(!strcmp(action,"execute")) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(args,"requestId"),*cap=cJSON_GetObjectItemCaseSensitive(args,"capability"),
            *argument=cJSON_GetObjectItemCaseSensitive(args,"argument");
        if(!id||!cap)error=ESP_ERR_INVALID_ARG;
        else {
            pet_command_request_t request={.id=(uint64_t)id->valuedouble,.device="mac"};
            snprintf(request.capability,sizeof(request.capability),"%s",cap->valuestring);
            if(argument)snprintf(request.argument,sizeof(request.argument),"%s",argument->valuestring);
            pet_ai_agent_snapshot_t ai;if(pet_ai_agent_snapshot(&ai)==ESP_OK)request.turn_id=ai.view.turn_id;
            error=pet_control_service_submit(&request);
        }
    } else if(!strcmp(action,"reconcile")) {
        const cJSON *id=cJSON_GetObjectItemCaseSensitive(args,"requestId");
        error=id?pet_control_service_reconcile((uint64_t)id->valuedouble):ESP_ERR_INVALID_ARG;
    }
    pet_control_view_t view={0};esp_err_t current=pet_control_service_snapshot(&view);
    cJSON *out=cJSON_CreateObject();cJSON_AddBoolToObject(out,"configured",view.configured);
    cJSON_AddBoolToObject(out,"accepted",error==ESP_OK&&current==ESP_OK);
    cJSON_AddBoolToObject(out,"awaitingLocalConfirmation",view.awaiting_confirmation);
    cJSON_AddNumberToObject(out,"nextRequestId",(double)view.next_request_id);
    cJSON_AddNumberToObject(out,"requestId",(double)view.last_job_id);cJSON_AddNumberToObject(out,"status",view.last_job_status);
    cJSON_AddStringToObject(out,"summary",view.result.summary);
    if(!strcmp(action,"discover")) {
        pet_control_catalog_t catalog;
        if(pet_control_service_catalog(&catalog)==ESP_OK) {
            cJSON *apps=cJSON_AddArrayToObject(out,"applications"),*projects=cJSON_AddArrayToObject(out,"projects");
            for(size_t i=0;i<catalog.application_count;++i)cJSON_AddItemToArray(apps,cJSON_CreateString(catalog.applications[i]));
            for(size_t i=0;i<catalog.project_count;++i)cJSON_AddItemToArray(projects,cJSON_CreateString(catalog.projects[i]));
        }
    }
    return out;
}
#endif
static cJSON *focus_tool(const cJSON *args,void *context)
{
    (void)context;
    const char *action=cJSON_GetObjectItemCaseSensitive(args,"action")->valuestring;
    esp_err_t error=!strcmp(action,"status")?ESP_OK:app_focus_command(action);
    pet_focus_t focus;
    if (pet_focus_snapshot(&focus)!=ESP_OK) return NULL;
    cJSON *out=cJSON_CreateObject();
    cJSON_AddBoolToObject(out,"ok",error==ESP_OK);cJSON_AddNumberToObject(out,"state",focus.state);
    cJSON_AddNumberToObject(out,"remainingSeconds",(double)((focus.remaining_ms+999)/1000));
    cJSON_AddBoolToObject(out,"rewardPending",focus.reward_pending);return out;
}
#define EMPTY "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}"
static const pet_intent_tool_t tools[]={
#if CONFIG_PET_EXT_DEVICE_CONTROL
    {"self.device_control","发现白名单、准备电脑控制、查询结果。app.open 必须在屏幕长按确认；unknown 只核对结果，不重试动作。",
        "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"discover\",\"status\",\"execute\",\"reconcile\"]},\"capability\":{\"type\":\"string\",\"enum\":[\"system.info\",\"project.status\",\"app.open\"]},\"argument\":{\"type\":\"string\",\"maxLength\":40},\"requestId\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991}},\"required\":[\"action\"],\"additionalProperties\":false}",true,device_tool,NULL},
#endif
#if CONFIG_PET_EXT_SERVO
    {"self.servo","校准并物理使能后控制舵机。accepted 仅代表排队，angleCommanded 无位置反馈；急停无需确认。",
        "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"status\",\"move\",\"home\",\"wag\",\"nod\",\"stop\",\"emergency\"]},\"requestId\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991},\"angle\":{\"type\":\"integer\",\"minimum\":0,\"maximum\":180},\"durationMs\":{\"type\":\"integer\",\"minimum\":100,\"maximum\":5000}},\"required\":[\"action\"],\"additionalProperties\":false}",true,servo_tool,NULL},
#endif
    {"self.focus","25 分钟专注计时，暂停不累计，取消不奖励",
        "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"start\",\"pause\",\"resume\",\"cancel\",\"status\"]}},\"required\":[\"action\"],\"additionalProperties\":false}",true,focus_tool,NULL},
    {"self.get_context","对话开始先读取：豆豆人设、最近对话与当前状态。历史只存在本机 RAM。",EMPTY,false,context_tool,NULL},
    {"self.get_status","读取豆豆当前成长与设备状态",EMPTY,false,state,NULL},
    {"self.get_unread","只读取未读数量，不打开消息正文",EMPTY,false,unread,NULL},
#if CONFIG_PET_MODULE_CLIMATE_ENABLED
    {"self.get_climate","读取室内温湿度，注意 valid 和 ageMs",EMPTY,false,climate_tool,NULL},
#endif
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    {"self.pet_interact","用户明确要求后照料豆豆。先读状态取得 nextRequestId，重试沿用原 ID",
        "{\"type\":\"object\",\"properties\":{\"action\":{\"type\":\"string\",\"enum\":[\"feed\",\"pet\",\"play\",\"sleep\",\"wake\"]},\"requestId\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":9007199254740991}},\"required\":[\"action\",\"requestId\"],\"additionalProperties\":false}",true,interact,NULL},
#endif
};
static bool handle(uint32_t epoch,const char *request,size_t bytes,char *out,size_t cap,size_t *written,void *ctx)
{
    (void)ctx;
    if (epoch!=connection_epoch) {pet_intent_new_connection(&rpc);connection_epoch=epoch;}
    return pet_intent_rpc(&rpc,request,bytes,out,cap,written);
}
esp_err_t app_tools_start(void)
{
    return pet_intent_init(&rpc,tools,sizeof(tools)/sizeof(tools[0])) ?
        pet_xz_wss_set_tools(handle,NULL) : ESP_FAIL;
}
#else
esp_err_t app_tools_start(void) { return ESP_OK; }
#endif
