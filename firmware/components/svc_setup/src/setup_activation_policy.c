#include "setup_activation.h"
#include "cJSON.h"
#include "pet/json_guard.h"
#include <stdbool.h>
#include <string.h>

static bool text(const cJSON *value,size_t max)
{
    if(!cJSON_IsString(value)||!value->valuestring[0]||strlen(value->valuestring)>max) return false;
    for(const unsigned char *p=(const unsigned char *)value->valuestring;*p;p++)
        if(*p<32||*p==127) return false;
    return true;
}

static void erase_tree(cJSON *root)
{
    for(cJSON *p=root;p;p=p->next) {
        erase_tree(p->child);
        if(p->valuestring) {volatile char *v=p->valuestring;size_t n=strlen(p->valuestring);while(n--) *v++=0;}
    }
}

esp_err_t pet_setup_activation_parse(const char *json,size_t bytes,pet_setup_activation_result_t *out)
{
    if(!out) return ESP_ERR_INVALID_ARG;
    memset(out,0,sizeof(*out));
    if(!json||!bytes||bytes>16384||memchr(json,0,bytes)||!pet_json_bounded(json,bytes,8)) return ESP_ERR_INVALID_ARG;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(json,bytes,&end,false);
    esp_err_t err=ESP_ERR_INVALID_ARG;
    while(root&&end<json+bytes&&(*end==' '||*end=='\t'||*end=='\r'||*end=='\n')) ++end;
    if(!cJSON_IsObject(root)||end!=json+bytes) goto done;
    cJSON *activation=cJSON_GetObjectItemCaseSensitive(root,"activation");
    if(activation) {
        cJSON *code=cJSON_GetObjectItemCaseSensitive(activation,"code");
        cJSON *challenge=cJSON_GetObjectItemCaseSensitive(activation,"challenge");
        if(!text(code,16)) goto done;
        for(const char *p=code->valuestring;*p;p++) if(*p<'0'||*p>'9') goto done;
        strcpy(out->code,code->valuestring);out->waiting=true;out->has_challenge=text(challenge,512);
        err=ESP_OK;goto done;
    }
    cJSON *websocket=cJSON_GetObjectItemCaseSensitive(root,"websocket");
    cJSON *uri=cJSON_GetObjectItemCaseSensitive(websocket,"url");
    cJSON *token=cJSON_GetObjectItemCaseSensitive(websocket,"token");
    if(!text(uri,255)||!text(token,512)||strncmp(uri->valuestring,"wss://",6)||!uri->valuestring[6]||
       strchr(uri->valuestring,'@')||strchr(uri->valuestring,'#')) goto done;
    strcpy(out->uri,uri->valuestring);strcpy(out->token,token->valuestring);err=ESP_OK;
done:
    erase_tree(root);cJSON_Delete(root);
    if(err!=ESP_OK) memset(out,0,sizeof(*out));
    return err;
}
