#include "setup_policy.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool clean(const char *value, size_t min, size_t max)
{
    if(!value) return false;
    size_t i=0;
    for(;i<=max&&value[i];i++)
        if((unsigned char)value[i]<32||(unsigned char)value[i]==127) return false;
    return i>=min&&i<=max;
}

bool pet_setup_wifi_valid(const char *ssid,const char *password)
{
    if(!clean(ssid,1,32)||!clean(password,0,64)) return false;
    size_t length=strlen(password);
    if(length&&length<8) return false;
    if(length==64) {
        for(size_t i=0;i<length;i++)
            if(!((password[i]>='0'&&password[i]<='9')||(password[i]>='a'&&password[i]<='f')||
                 (password[i]>='A'&&password[i]<='F'))) return false;
    }
    return true;
}

bool pet_setup_request_authorized(const char *host,const char *origin,
                                  const char *token,const char *expected,bool mutation)
{
    if(!host||(strcmp(host,PET_SETUP_HOST)&&strcmp(host,PET_SETUP_HOST ":80"))) return false;
    if(origin&&origin[0]&&strcmp(origin,PET_SETUP_ORIGIN)) return false;
    if(!mutation) return true;
    if(!origin||strcmp(origin,PET_SETUP_ORIGIN)||!token||!expected||strlen(expected)!=32||strlen(token)!=32) return false;
    unsigned difference=0;
    for(size_t i=0;i<32;i++) difference|=(unsigned char)token[i]^(unsigned char)expected[i];
    return difference==0;
}

const char *pet_setup_state_name(pet_setup_state_t state)
{
    static const char *const names[]={"starting","ready","connecting","wifi_saved","activating",
        "waiting_bind","voice_saved","services_saved","portal_saved","network_saved","failed","leaving"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"failed";
}

void pet_setup_portal_default(pet_setup_portal_t *portal)
{
    memset(portal,0,sizeof(*portal));
    portal->password_mode=PET_SETUP_PASSWORD_AUTO;
    portal->channel=PET_SETUP_CHANNEL_MIN;
    portal->max_clients=2;
    portal->window_s=PET_SETUP_WINDOW_S;
}

bool pet_setup_portal_valid(const pet_setup_portal_t *portal)
{
    if(!portal) return false;
    if(portal->custom&&!clean(portal->ssid,1,PET_SETUP_SSID_MAX)) return false;
    if(portal->password_mode==PET_SETUP_PASSWORD_CUSTOM) {
        if(!clean(portal->password,8,PET_SETUP_PASSWORD_MAX)) return false;
    } else if(portal->password_mode!=PET_SETUP_PASSWORD_AUTO&&portal->password_mode!=PET_SETUP_PASSWORD_OPEN) {
        return false;
    }
    return portal->channel>=PET_SETUP_CHANNEL_MIN&&portal->channel<=PET_SETUP_CHANNEL_MAX&&
        portal->max_clients>=PET_SETUP_CLIENTS_MIN&&portal->max_clients<=PET_SETUP_CLIENTS_MAX&&
        portal->window_s>=PET_SETUP_WINDOW_MIN_S&&portal->window_s<=PET_SETUP_WINDOW_MAX_S&&
        portal->window_s%60u==0; /* 页面与 NVS 都按整分钟表达，避免回读丢精度。 */
}

void pet_setup_portal_effective(const pet_setup_portal_t *portal,const uint8_t random[32],
                                char ssid[PET_SETUP_SSID_MAX+1],char password[PET_SETUP_PASSWORD_MAX+1])
{
    if(portal->custom) snprintf(ssid,PET_SETUP_SSID_MAX+1,"%s",portal->ssid);
    else snprintf(ssid,PET_SETUP_SSID_MAX+1,"Qitomo-%02X%02X",random[0],random[1]);
    if(portal->password_mode==PET_SETUP_PASSWORD_CUSTOM) {
        snprintf(password,PET_SETUP_PASSWORD_MAX+1,"%s",portal->password);
    } else if(portal->password_mode==PET_SETUP_PASSWORD_AUTO) {
        static const char alphabet[]="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
        for(unsigned i=0;i<12;i++) password[i]=alphabet[random[i+2]&31];
        password[12]=0;
    } else password[0]=0;
}

bool pet_setup_portal_format(const pet_setup_portal_t *portal,char *out,size_t capacity)
{
    if(!portal||!out||!capacity||!pet_setup_portal_valid(portal)) return false;
    cJSON *root=cJSON_CreateObject();
    if(!root) return false;
    bool ok=cJSON_AddNumberToObject(root,"version",1)&&
        cJSON_AddBoolToObject(root,"custom",portal->custom)&&
        cJSON_AddNumberToObject(root,"passwordMode",portal->password_mode)&&
        cJSON_AddBoolToObject(root,"hidden",portal->hidden)&&
        cJSON_AddNumberToObject(root,"channel",portal->channel)&&
        cJSON_AddNumberToObject(root,"maxClients",portal->max_clients)&&
        cJSON_AddNumberToObject(root,"windowMinutes",portal->window_s/60u)&&
        (!portal->custom||cJSON_AddStringToObject(root,"ssid",portal->ssid))&&
        (portal->password_mode!=PET_SETUP_PASSWORD_CUSTOM||cJSON_AddStringToObject(root,"password",portal->password));
    if(ok) {
        char *json=cJSON_PrintUnformatted(root);
        ok=json&&strlen(json)<capacity;
        if(ok) strcpy(out,json);
        if(json) {
            volatile unsigned char *secret=(volatile unsigned char *)json;
            for(size_t i=0;i<strlen(json);i++) secret[i]=0;
            cJSON_free(json);
        }
    }
    cJSON_Delete(root);return ok;
}

static bool number(const cJSON *item,double min,double max,double *out)
{
    if(!cJSON_IsNumber(item)||!isfinite(item->valuedouble)) return false;
    double value=item->valuedouble;
    if(value<min||value>max||floor(value)!=value) return false;
    *out=value;return true;
}

static bool parse_portal(const char *json,size_t bytes,pet_setup_portal_t *out,bool allow_blank)
{
    if(!json||!bytes||bytes>512||!out) return false;
    const char *end=NULL;
    cJSON *root=cJSON_ParseWithLengthOpts(json,bytes,&end,false);
    if(!cJSON_IsObject(root)||end!=json+bytes) {cJSON_Delete(root);return false;}
    pet_setup_portal_t portal;
    pet_setup_portal_default(&portal);
    bool ok=true;double value=0;
    /* 未知/重复字段、缺字段与错误类型都拒绝，避免新旧页面互相静默覆盖。 */
    static const char *const keys[]={"version","custom","ssid","passwordMode","password","hidden",
        "channel","maxClients","windowMinutes"};
    unsigned mask=0;
    for(const cJSON *item=root->child;item&&ok;item=item->next) {
        unsigned i=0;
        while(i<sizeof(keys)/sizeof(keys[0])&&(!item->string||strcmp(keys[i],item->string))) i++;
        if(i==sizeof(keys)/sizeof(keys[0])||(mask&(1u<<i))) {ok=false;break;}
        mask|=1u<<i;
        if(!strcmp(keys[i],"version")) ok=number(item,1,1,&value);
        else if(!strcmp(keys[i],"custom")) ok=cJSON_IsBool(item)?(portal.custom=cJSON_IsTrue(item),true):false;
        else if(!strcmp(keys[i],"hidden")) ok=cJSON_IsBool(item)?(portal.hidden=cJSON_IsTrue(item),true):false;
        else if(!strcmp(keys[i],"ssid")) ok=cJSON_IsString(item)&&strlen(item->valuestring)<=PET_SETUP_SSID_MAX;
        else if(!strcmp(keys[i],"password")) ok=cJSON_IsString(item)&&strlen(item->valuestring)<=PET_SETUP_PASSWORD_MAX;
        else if(!strcmp(keys[i],"passwordMode")) ok=number(item,0,2,&value);
        else if(!strcmp(keys[i],"channel")) ok=number(item,PET_SETUP_CHANNEL_MIN,PET_SETUP_CHANNEL_MAX,&value);
        else if(!strcmp(keys[i],"maxClients")) ok=number(item,PET_SETUP_CLIENTS_MIN,PET_SETUP_CLIENTS_MAX,&value);
        else ok=number(item,PET_SETUP_WINDOW_MIN_S/60u,PET_SETUP_WINDOW_MAX_S/60u,&value);
        if(ok) {
            if(!strcmp(keys[i],"ssid")) strcpy(portal.ssid,item->valuestring);
            else if(!strcmp(keys[i],"password")) strcpy(portal.password,item->valuestring);
            else if(!strcmp(keys[i],"passwordMode")) portal.password_mode=(pet_setup_password_mode_t)(int)value;
            else if(!strcmp(keys[i],"channel")) portal.channel=(uint8_t)value;
            else if(!strcmp(keys[i],"maxClients")) portal.max_clients=(uint8_t)value;
            else if(!strcmp(keys[i],"windowMinutes")) portal.window_s=(uint16_t)(value*60);
        }
    }
    cJSON_Delete(root);
    /* 必填：version/custom/passwordMode/hidden/channel/maxClients/windowMinutes；ssid 与 password 跟随策略。 */
    const unsigned required=(1u<<0)|(1u<<1)|(1u<<3)|(1u<<5)|(1u<<6)|(1u<<7)|(1u<<8);
    if(!ok||(mask&required)!=required) return false;
    if(portal.password_mode!=PET_SETUP_PASSWORD_CUSTOM) portal.password[0]=0;
    if(!portal.custom) portal.ssid[0]=0;
    if(!allow_blank&&!pet_setup_portal_valid(&portal)) return false;
    *out=portal;return true;
}

bool pet_setup_portal_parse(const char *json,size_t bytes,pet_setup_portal_t *out)
{
    return parse_portal(json,bytes,out,false);
}

bool pet_setup_portal_parse_partial(const char *json,size_t bytes,pet_setup_portal_t *out)
{
    return parse_portal(json,bytes,out,true);
}

void pet_setup_netcfg_default(pet_setup_netcfg_t *netcfg)
{
    memset(netcfg,0,sizeof(*netcfg));
    strcpy(netcfg->ip_mode,"dhcp");
}

bool pet_setup_netcfg_valid(const pet_setup_netcfg_t *netcfg)
{
    if(!netcfg) return false;
    if(!pet_netcfg_mode(netcfg->ip_mode)||!netcfg->ip_mode[0]) return false;
    uint8_t quad[4];
    if(pet_netcfg_static(netcfg->ip_mode)) {
        if(!pet_netcfg_ipv4(netcfg->ip,quad)||!pet_netcfg_netmask(netcfg->netmask)||
            !pet_netcfg_ipv4(netcfg->gateway,quad)) return false;
        if(!pet_netcfg_same_subnet(netcfg->ip,netcfg->netmask,netcfg->gateway)) return false;
        if(netcfg->dns[0]&&!pet_netcfg_ipv4(netcfg->dns,quad)) return false;
    } else if(netcfg->ip[0]||netcfg->netmask[0]||netcfg->gateway[0]||netcfg->dns[0]) {
        return false;
    }
    return !netcfg->hostname[0]||pet_netcfg_hostname(netcfg->hostname);
}
