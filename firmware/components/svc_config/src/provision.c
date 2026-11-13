#include "svc_config/provision.h"
#include "pet/json_guard.h"
#include "svc_config/api.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>
#include "cJSON.h"
#include "pet/storage.h"
#include "nvs.h"

static void wipe(void *p, size_t bytes)
{ volatile unsigned char *b = p; while (bytes--) *b++ = 0; }
static atomic_flag saving = ATOMIC_FLAG_INIT;
static void wipe_tree(cJSON *root)
{
    for (cJSON *p=root; p; p=p->next) {
        wipe_tree(p->child);
        if (p->valuestring) wipe(p->valuestring,strlen(p->valuestring));
    }
}
static void secure_delete(cJSON *root) { wipe_tree(root); cJSON_Delete(root); }
static bool plain(const char *text, size_t max, bool pem)
{
    if (strlen(text) > max) return false;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p)
        if (*p < 32 && !(pem && (*p == '\n' || *p == '\r'))) return false;
    return true;
}
static bool group(const cJSON *root, const char *name, const char *const *keys, const size_t *limits,
                  size_t count, unsigned required)
{
    const cJSON *object = cJSON_GetObjectItemCaseSensitive(root, name);
    if (!object) return required == 0;
    if (!cJSON_IsObject(object)) return false;
    unsigned mask = 0;
    for (const cJSON *p = object->child; p; p = p->next) {
        size_t i = 0; while (i < count && strcmp(keys[i], p->string)) ++i;
        if (i == count || (mask & (1u<<i)) || !cJSON_IsString(p) ||
            !plain(p->valuestring, limits[i], strstr(keys[i], "ca") != NULL)) return false;
        if ((required & (1u<<i)) && !p->valuestring[0]) return false;
        mask |= 1u<<i;
    }
    return (mask & required) == required;
}
static bool valid(const cJSON *root)
{
    static const char *const net[] = {"wifi_ssid", "wifi_pass"};
    static const size_t nl[] = {32,64};
    static const char *const nexus[] = {"device_id", "mqtt_uri", "mqtt_user", "mqtt_pass", "mqtt_ca"};
    static const size_t ml[] = {40,127,64,128,3000};
    static const char *const ai[] = {"ws_uri", "ws_token", "device_id", "client_id", "ws_ca"};
    static const size_t al[] = {255,512,64,64,4096};
    static const char *const control[]={"api_uri","token","ca","allowed_apps","allowed_projects"};
    static const size_t cl[]={255,128,3000,655,655};
    if (!cJSON_IsObject(root)) return false;
    unsigned mask = 0;
    for (const cJSON *p = root->child; p; p=p->next) {
        unsigned bit = !strcmp(p->string,"net") ? 1 : !strcmp(p->string,"nexus") ? 2 : !strcmp(p->string,"xiaozhi") ? 4 : !strcmp(p->string,"control") ? 8 : 0;
        if (!bit || (mask&bit)) return false;
        mask |= bit;
    }
    if ((mask&3) != 3 || !group(root,"net",net,nl,2,1) || !group(root,"nexus",nexus,ml,5,15) ||
        !group(root,"xiaozhi",ai,al,5,(mask&4) ? 15 : 0)||!group(root,"control",control,cl,5,(mask&8)?3:0)) return false;
    const cJSON *n = cJSON_GetObjectItemCaseSensitive(root,"nexus");
    const char *id = cJSON_GetObjectItemCaseSensitive(n,"device_id")->valuestring;
    for (const char *p=id; *p; ++p)
        if (!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||*p=='-'||*p=='_')) return false;
    const char *uri = cJSON_GetObjectItemCaseSensitive(n,"mqtt_uri")->valuestring;
    if (strncmp(uri,"mqtts://",8) || !uri[8] || strstr(uri,"@") || strstr(uri,"#")) return false;
    if (mask&4) {
        const cJSON *a = cJSON_GetObjectItemCaseSensitive(root,"xiaozhi");
        uri = cJSON_GetObjectItemCaseSensitive(a,"ws_uri")->valuestring;
        if (strncmp(uri,"wss://",6) || !uri[6] || strstr(uri,"@") || strstr(uri,"#")) return false;
    }
    if(mask&8) {
        const cJSON *c=cJSON_GetObjectItemCaseSensitive(root,"control");
        uri=cJSON_GetObjectItemCaseSensitive(c,"api_uri")->valuestring;
        if(strncmp(uri,"https://",8)||!uri[8]||strstr(uri,"@")||strstr(uri,"#")||strstr(uri,"?"))return false;
        if(strlen(cJSON_GetObjectItemCaseSensitive(c,"token")->valuestring)<32)return false;
    }
    return true;
}
static cJSON *parse(const char *json, size_t bytes)
{
    if (!json || !bytes || bytes>PET_PROVISION_JSON_MAX || !pet_json_bounded(json,bytes,4)) return NULL;
    char *copy = malloc(bytes+1);
    if (!copy) return NULL;
    memcpy(copy,json,bytes); copy[bytes]=0;
    const char *end;
    cJSON *root = strstr(copy,"\\u0000") ? NULL : cJSON_ParseWithLengthOpts(copy,bytes,&end,false);
    while (root && end<copy+bytes && (*end==' '||*end=='\t'||*end=='\r'||*end=='\n')) ++end;
    if (root && end!=copy+bytes) { secure_delete(root); root=NULL; }
    wipe(copy,bytes); free(copy); return root;
}
static esp_err_t load(cJSON **root, uint32_t *revision)
{
    unsigned char *data = malloc(PET_PROVISION_JSON_MAX+4);
    if (!data) return ESP_ERR_NO_MEM;
    size_t bytes;
    esp_err_t e = pet_storage_load("pet_provision","bundle_v1",data,PET_PROVISION_JSON_MAX+4,&bytes);
    if (e==ESP_OK) {
        if (bytes<=4) e=ESP_ERR_INVALID_STATE;
        else {
            *revision=(uint32_t)data[0]|(uint32_t)data[1]<<8|(uint32_t)data[2]<<16|(uint32_t)data[3]<<24;
            *root=parse((const char *)data+4,bytes-4);
            if (!*revision || !valid(*root)) { secure_delete(*root); *root=NULL; e=ESP_ERR_INVALID_STATE; }
        }
    }
    wipe(data,PET_PROVISION_JSON_MAX+4); free(data); return e;
}
esp_err_t pet_provision_revision(uint32_t *revision)
{
    if (!revision) return ESP_ERR_INVALID_ARG;
    cJSON *root=NULL; *revision=0;
    esp_err_t e=load(&root,revision); secure_delete(root);
    return e==ESP_ERR_NVS_NOT_FOUND ? ESP_OK : e;
}
esp_err_t pet_provision_load_str(const char *ns, const char *key, char *out, size_t cap)
{
    if (!ns||!key||!out||!cap) return ESP_ERR_INVALID_ARG;
    cJSON *root=NULL; uint32_t revision=0;
    esp_err_t e=load(&root,&revision);
    if (e==ESP_ERR_NVS_NOT_FOUND) return pet_storage_load_str(ns,key,out,cap);
    if (e==ESP_OK) {
        const cJSON *value=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,ns),key);
        bool optional=!strcmp(key,"ws_ca")||!strcmp(key,"mqtt_ca")||!strcmp(key,"wifi_pass")||
            (!strcmp(ns,"control")&&(!strcmp(key,"ca")||!strcmp(key,"allowed_apps")||!strcmp(key,"allowed_projects")));
        e=!cJSON_IsString(value) ? optional ? ESP_ERR_NVS_NOT_FOUND : ESP_ERR_INVALID_STATE :
            strlen(value->valuestring)>=cap ? ESP_ERR_INVALID_SIZE : ESP_OK;
        if (e==ESP_OK) strcpy(out,value->valuestring);
    }
    secure_delete(root); return e;
}
esp_err_t pet_provision_save(const char *json, size_t bytes, uint32_t expected)
{
    if (atomic_flag_test_and_set(&saving)) return ESP_ERR_TIMEOUT;
    cJSON *root=parse(json,bytes), *old=NULL; uint32_t current=0;
    esp_err_t e=ESP_ERR_INVALID_ARG;
    if (!valid(root)) goto done;
    e=load(&old,&current);
    if (e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND) goto done;
    if (expected!=current || current==UINT32_MAX) { e=ESP_ERR_INVALID_STATE; goto done; }
    const char *id=cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(root,"nexus"),"device_id")->valuestring;
    char old_id[41];
    e=pet_provision_load_str("nexus","device_id",old_id,sizeof(old_id));
    if ((e==ESP_OK && strcmp(id,old_id)) || (e!=ESP_OK && e!=ESP_ERR_NVS_NOT_FOUND)) { e=ESP_ERR_INVALID_STATE; goto done; }
    unsigned char *data=malloc(bytes+4), *readback=malloc(bytes+4); size_t check_size=0;
    if (!data||!readback) { free(data); free(readback); e=ESP_ERR_NO_MEM; goto done; }
    uint32_t next=current+1;
    for (unsigned i=0;i<4;++i) data[i]=(unsigned char)(next>>(i*8));
    memcpy(data+4,json,bytes);
    e=pet_storage_save("pet_provision","bundle_v1",data,bytes+4);
    esp_err_t checked=pet_storage_load("pet_provision","bundle_v1",readback,bytes+4,&check_size);
    if (checked==ESP_OK&&check_size==bytes+4&&!memcmp(data,readback,check_size)) e=ESP_OK;
    else if (e==ESP_OK) e=ESP_FAIL;
    wipe(data,bytes+4); wipe(readback,bytes+4); free(data); free(readback);
done:
    secure_delete(root); secure_delete(old); atomic_flag_clear(&saving); return e;
}
esp_err_t pet_config_command(const char *line, size_t bytes, char *out, size_t cap)
{
    if (!line||!out||cap<32||!bytes) return ESP_ERR_INVALID_ARG;
    esp_err_t e=ESP_ERR_INVALID_ARG;
    const char *settings="PET_SETTINGS ", *provision="PET_PROVISION ";
    if (bytes==20&&!memcmp(line,"PET_PROVISION_STATUS",20)) {
        uint32_t revision;
        e=pet_provision_revision(&revision);
        if (e==ESP_OK) { snprintf(out,cap,"{\"ok\":true,\"revision\":%lu}",(unsigned long)revision); return ESP_OK; }
    } else if (bytes==16&&!memcmp(line,"PET_SETTINGS_GET",16)) {
        size_t n;
        pet_config_snapshot_t snapshot;
        e=pet_config_snapshot(&snapshot);
        int prefix=e==ESP_OK ? snprintf(out,cap,"{\"revision\":%lu,\"writable\":%s,\"config\":",
            (unsigned long)snapshot.revision,snapshot.writable?"true":"false") : -1;
        if (prefix<0 || (size_t)prefix>=cap) e=ESP_ERR_INVALID_SIZE;
        if (prefix>=0&&(size_t)prefix<cap) {
            e=pet_config_format_json(&snapshot.config,out+prefix,cap-(size_t)prefix,&n);
            if (e==ESP_OK&&n+(size_t)prefix+2<=cap) { out[prefix+n]='}'; out[prefix+n+1]=0; return ESP_OK; }
        }
    } else {
        size_t prefix=bytes>strlen(settings)&&!memcmp(line,settings,strlen(settings)) ? strlen(settings) :
            bytes>strlen(provision)&&!memcmp(line,provision,strlen(provision)) ? strlen(provision) : 0;
        cJSON *root=prefix ? parse(line+prefix,bytes-prefix) : NULL;
        const cJSON *r=cJSON_GetObjectItemCaseSensitive(root,"expectedRevision");
        const char *name=prefix==strlen(settings)?"config":"credentials";
        const cJSON *value=cJSON_GetObjectItemCaseSensitive(root,name);
        if (cJSON_IsObject(root)&&cJSON_GetArraySize(root)==2&&cJSON_IsNumber(r)&&isfinite(r->valuedouble)&&
            r->valuedouble>=0&&r->valuedouble<UINT32_MAX&&floor(r->valuedouble)==r->valuedouble&&cJSON_IsObject(value)) {
            char *encoded=cJSON_PrintUnformatted(value);
            if (encoded) {
                e=prefix==strlen(settings) ? pet_config_apply_json(encoded,strlen(encoded),(uint32_t)r->valuedouble) :
                    pet_provision_save(encoded,strlen(encoded),(uint32_t)r->valuedouble);
                wipe(encoded,strlen(encoded)); cJSON_free(encoded);
            } else e=ESP_ERR_NO_MEM;
        }
        secure_delete(root);
    }
    snprintf(out,cap,"{\"ok\":%s,\"code\":%d,\"restartRequired\":%s}",e==ESP_OK?"true":"false",e,
        e==ESP_OK&&bytes>=14&&!memcmp(line,"PET_PROVISION ",14)?"true":"false");
    return e;
}
