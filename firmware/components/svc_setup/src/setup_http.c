#include "setup_internal.h"
#include "setup_policy.h"
#include "svc_config/provision.h"
#include "pet/json_guard.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <math.h>
#include <net/if.h>
#include <string.h>

extern const char setup_html_start[] asm("_binary_setup_html_start");
extern const char setup_html_end[] asm("_binary_setup_html_end");

static void headers(httpd_req_t *r)
{
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"Referrer-Policy","no-referrer");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; form-action 'self'; frame-ancestors 'none'");
}

static esp_err_t json_response(httpd_req_t *r,const char *status,const char *text)
{
    headers(r);httpd_resp_set_status(r,status);httpd_resp_set_type(r,"application/json; charset=utf-8");
    return httpd_resp_sendstr(r,text);
}

static bool authorized(httpd_req_t *r)
{
    setup_runtime_t *s=r->user_ctx;
    char host[64]={0},origin[96]={0},token[40]={0};
    if(httpd_req_get_hdr_value_str(r,"Host",host,sizeof(host))!=ESP_OK) return false;
    size_t origin_bytes=httpd_req_get_hdr_value_len(r,"Origin");
    if(origin_bytes&&httpd_req_get_hdr_value_str(r,"Origin",origin,sizeof(origin))!=ESP_OK) return false;
    if(r->method==HTTP_POST&&httpd_req_get_hdr_value_str(r,"X-Setup-Token",token,sizeof(token))!=ESP_OK) return false;
    return !setup_cancelled(s)&&pet_setup_request_authorized(host,origin,token,s->session,r->method==HTTP_POST);
}

static esp_err_t page(httpd_req_t *r)
{
    if(!authorized(r)) return json_response(r,"403 Forbidden","{\"error\":\"forbidden\"}");
    headers(r);httpd_resp_set_type(r,"text/html; charset=utf-8");
    return httpd_resp_send(r,setup_html_start,setup_html_end-setup_html_start);
}

static esp_err_t status(httpd_req_t *r)
{
    if(!authorized(r)) return json_response(r,"403 Forbidden","{\"error\":\"forbidden\"}");
    setup_runtime_t *s=r->user_ctx;
    pet_setup_snapshot_t snapshot;
    if(pet_setup_snapshot(&snapshot)!=ESP_OK) return json_response(r,"503 Service Unavailable","{\"error\":\"starting\"}");
    cJSON *root=cJSON_CreateObject();
    if(!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root,"state",pet_setup_state_name(snapshot.state));
    cJSON_AddStringToObject(root,"session",s->session);
    cJSON_AddNumberToObject(root,"revision",snapshot.revision);
    cJSON_AddNumberToObject(root,"remainingSeconds",snapshot.remaining_s);
    cJSON_AddNumberToObject(root,"errorCode",snapshot.error);
    cJSON_AddBoolToObject(root,"busy",atomic_load(&s->busy));
    cJSON_AddBoolToObject(root,"wifiSaved",snapshot.wifi_saved);
    cJSON_AddBoolToObject(root,"wifiConnected",(xEventGroupGetBits(s->network_events)&SETUP_CONNECTED)!=0);
    cJSON_AddStringToObject(root,"wifiSsid",snapshot.wifi_ssid);
    cJSON_AddBoolToObject(root,"voiceSaved",snapshot.voice_saved);
    cJSON_AddBoolToObject(root,"nexusSaved",snapshot.nexus_saved);
    cJSON_AddStringToObject(root,"activationCode",snapshot.activation_code);
    /* 只回显热点名称与安全形态；热点口令、家庭凭据仍只留在设备和屏幕上。 */
    cJSON_AddStringToObject(root,"apSsid",snapshot.ap_ssid);
    cJSON_AddBoolToObject(root,"apOpen",snapshot.ap_password[0]==0);
    cJSON *portal=cJSON_AddObjectToObject(root,"portal");
    if(portal) {
        const pet_setup_portal_t *saved=&snapshot.portal;
        cJSON_AddBoolToObject(portal,"custom",saved->custom);
        cJSON_AddNumberToObject(portal,"passwordMode",saved->password_mode);
        cJSON_AddBoolToObject(portal,"hidden",saved->hidden);
        cJSON_AddNumberToObject(portal,"channel",saved->channel);
        cJSON_AddNumberToObject(portal,"maxClients",saved->max_clients);
        cJSON_AddNumberToObject(portal,"windowMinutes",saved->window_s/60u);
        if(saved->custom) cJSON_AddStringToObject(portal,"ssid",saved->ssid);
    }
    cJSON *network=cJSON_AddObjectToObject(root,"network");
    if(network) {
        cJSON_AddStringToObject(network,"ipMode",snapshot.netcfg.ip_mode);
        cJSON_AddStringToObject(network,"ip",snapshot.netcfg.ip);
        cJSON_AddStringToObject(network,"netmask",snapshot.netcfg.netmask);
        cJSON_AddStringToObject(network,"gateway",snapshot.netcfg.gateway);
        cJSON_AddStringToObject(network,"dns",snapshot.netcfg.dns);
        cJSON_AddStringToObject(network,"hostname",snapshot.netcfg.hostname);
    }
    /* 当前地址只读回显，便于确认固定地址/DHCP 结果和信号质量。 */
    cJSON *sta=cJSON_AddObjectToObject(root,"sta");
    if(sta) {
        char text[16]={0};
        esp_netif_ip_info_t info={0};
        if(s->sta&&esp_netif_get_ip_info(s->sta,&info)==ESP_OK&&info.ip.addr) {
            esp_ip4addr_ntoa(&info.ip,text,sizeof(text));
            cJSON_AddStringToObject(sta,"ip",text);
            esp_ip4addr_ntoa(&info.gw,text,sizeof(text));
            cJSON_AddStringToObject(sta,"gateway",text);
            esp_ip4addr_ntoa(&info.netmask,text,sizeof(text));
            cJSON_AddStringToObject(sta,"netmask",text);
        }
        esp_netif_dns_info_t dns={0};
        if(s->sta&&esp_netif_get_dns_info(s->sta,ESP_NETIF_DNS_MAIN,&dns)==ESP_OK&&dns.ip.u_addr.ip4.addr) {
            esp_ip4addr_ntoa(&dns.ip.u_addr.ip4,text,sizeof(text));
            cJSON_AddStringToObject(sta,"dns",text);
        }
        wifi_ap_record_t ap={0};
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK) {
            cJSON_AddNumberToObject(sta,"rssi",ap.rssi);
            cJSON_AddNumberToObject(sta,"channel",ap.primary);
        }
    }
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(!json) return ESP_ERR_NO_MEM;
    esp_err_t err=json_response(r,"200 OK",json);cJSON_free(json);return err;
}

static esp_err_t scan(httpd_req_t *r)
{
    if(!authorized(r)) return json_response(r,"403 Forbidden","{\"error\":\"forbidden\"}");
    setup_runtime_t *s=r->user_ctx;
    if(atomic_load(&s->busy)) return json_response(r,"409 Conflict","{\"error\":\"busy\"}");
    wifi_scan_config_t config={.show_hidden=false,.scan_time.active={.min=40,.max=80}};
    esp_err_t err=esp_wifi_scan_start(&config,true);
    if(err!=ESP_OK) return json_response(r,"503 Service Unavailable","{\"error\":\"scan_failed\"}");
    wifi_ap_record_t *aps=heap_caps_calloc(PET_SETUP_AP_MAX,sizeof(*aps),MALLOC_CAP_8BIT);
    if(!aps) return ESP_ERR_NO_MEM;
    uint16_t count=PET_SETUP_AP_MAX;
    err=esp_wifi_scan_get_ap_records(&count,aps);
    cJSON *root=cJSON_CreateObject(),*items=root?cJSON_AddArrayToObject(root,"networks"):NULL;
    if(err==ESP_OK&&items) for(unsigned i=0;i<count;i++) {
        char ssid[33]={0};memcpy(ssid,aps[i].ssid,32);
        if(!ssid[0]) continue;
        cJSON *item=cJSON_CreateObject();
        if(!item) {err=ESP_ERR_NO_MEM;break;}
        cJSON_AddStringToObject(item,"ssid",ssid);cJSON_AddNumberToObject(item,"rssi",aps[i].rssi);
        cJSON_AddBoolToObject(item,"secured",aps[i].authmode!=WIFI_AUTH_OPEN);cJSON_AddItemToArray(items,item);
    }
    char *json=items&&err==ESP_OK?cJSON_PrintUnformatted(root):NULL;
    cJSON_Delete(root);heap_caps_free(aps);
    if(!json) return json_response(r,"503 Service Unavailable","{\"error\":\"scan_failed\"}");
    err=json_response(r,"200 OK",json);cJSON_free(json);return err;
}

static bool text_field(const cJSON *object,const char *name,char *out,size_t capacity)
{
    const cJSON *item=cJSON_GetObjectItemCaseSensitive(object,name);
    if(!item) {out[0]=0;return true;}
    if(!cJSON_IsString(item)||strlen(item->valuestring)>=capacity) return false;
    strcpy(out,item->valuestring);return true;
}

static void wipe_tree(cJSON *root)
{
    for(cJSON *p=root;p;p=p->next) {wipe_tree(p->child);if(p->valuestring)setup_wipe(p->valuestring,strlen(p->valuestring));}
}

static esp_err_t mutate(httpd_req_t *r)
{
    if(!authorized(r)) return json_response(r,"403 Forbidden","{\"error\":\"forbidden\"}");
    setup_runtime_t *s=r->user_ctx;
    char type[64]={0};
    if(httpd_req_get_hdr_value_str(r,"Content-Type",type,sizeof(type))!=ESP_OK||
        (strcmp(type,"application/json")&&strcmp(type,"application/json; charset=utf-8")))
        return json_response(r,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(r->content_len==0||r->content_len>PET_PROVISION_JSON_MAX)
        return json_response(r,"400 Bad Request","{\"error\":\"invalid_request\"}");
    size_t bytes=r->content_len;
    char *body=heap_caps_malloc(bytes+1,MALLOC_CAP_8BIT);
    if(!body) return ESP_ERR_NO_MEM;
    size_t received=0;esp_err_t err=ESP_ERR_INVALID_ARG;
    while(received<bytes) {
        int n=httpd_req_recv(r,body+received,bytes-received);
        if(n<=0) goto done;
        received+=(size_t)n;
    }
    body[bytes]=0;
    cJSON *root=NULL;
    if(memchr(body,0,bytes)||strstr(body,"\\u0000")||!pet_json_bounded(body,bytes,5)) goto done;
    const char *end=NULL;
    root=cJSON_ParseWithLengthOpts(body,bytes,&end,false);
    if(!cJSON_IsObject(root)||end!=body+bytes) goto parsed;
    cJSON *version=cJSON_GetObjectItemCaseSensitive(root,"expectedRevision");
    if(!cJSON_IsNumber(version)||version->valuedouble<0||version->valuedouble>=UINT32_MAX||
        floor(version->valuedouble)!=version->valuedouble) goto parsed;
    uint32_t expected=(uint32_t)version->valuedouble,current=0;
    if(pet_provision_revision(&current)!=ESP_OK||current!=expected) {err=ESP_ERR_INVALID_STATE;goto parsed;}
    if(!strcmp(r->uri,"/api/wifi")) {
        cJSON *ssid=cJSON_GetObjectItemCaseSensitive(root,"ssid"),*password=cJSON_GetObjectItemCaseSensitive(root,"password");
        if(cJSON_GetArraySize(root)!=3||!cJSON_IsString(ssid)||!cJSON_IsString(password)) goto parsed;
        err=pet_setup_submit_wifi(ssid->valuestring,password->valuestring,expected);
    } else if(!strcmp(r->uri,"/api/services")) {
        cJSON *nexus=cJSON_GetObjectItemCaseSensitive(root,"nexus");
        if(cJSON_GetArraySize(root)!=2||!cJSON_IsObject(nexus)) goto parsed;
        cJSON *patch=cJSON_CreateObject();
        cJSON *copy=cJSON_Duplicate(nexus,true);
        if(!patch||!copy||!cJSON_AddItemToObject(patch,"nexus",copy)) {cJSON_Delete(copy);cJSON_Delete(patch);err=ESP_ERR_NO_MEM;goto parsed;}
        char *json=cJSON_PrintUnformatted(patch);
        if(json) {err=pet_setup_submit_services(json,strlen(json),expected);setup_wipe(json,strlen(json));cJSON_free(json);}
        wipe_tree(patch);cJSON_Delete(patch);
    } else if(!strcmp(r->uri,"/api/activate")) {
        if(cJSON_GetArraySize(root)==1) err=pet_setup_activate();
    } else if(!strcmp(r->uri,"/api/portal")) {
        cJSON *portal=cJSON_GetObjectItemCaseSensitive(root,"portal");
        cJSON *restart=cJSON_GetObjectItemCaseSensitive(root,"restart");
        int size=cJSON_GetArraySize(root);
        if(!cJSON_IsObject(portal)||!(size==2||(size==3&&cJSON_IsBool(restart)))) goto parsed;
        char *encoded=cJSON_PrintUnformatted(portal);
        if(!encoded) {err=ESP_ERR_NO_MEM;goto parsed;}
        size_t bytes=strlen(encoded);
        pet_setup_portal_t settings,saved;
        xSemaphoreTake(s->lock,portMAX_DELAY);saved=s->portal;xSemaphoreGive(s->lock);
        /* 页面不回显已保存的名称/口令：留空表示沿用，其它字段与 NVS 编解码共用同一套边界。 */
        if(bytes<=512&&pet_setup_portal_parse_partial(encoded,bytes,&settings)) {
            if(settings.custom&&!settings.ssid[0]) memcpy(settings.ssid,saved.ssid,sizeof(settings.ssid));
            if(settings.password_mode==PET_SETUP_PASSWORD_CUSTOM&&!settings.password[0])
                memcpy(settings.password,saved.password,sizeof(settings.password));
            if(pet_setup_portal_valid(&settings))
                err=pet_setup_submit_portal(&settings,cJSON_IsTrue(restart),expected);
            setup_wipe(&settings,sizeof(settings));
        }
        setup_wipe(&saved,sizeof(saved));
        setup_wipe(encoded,bytes);cJSON_free(encoded);
    } else if(!strcmp(r->uri,"/api/network")) {
        cJSON *network=cJSON_GetObjectItemCaseSensitive(root,"network");
        if(cJSON_GetArraySize(root)!=2||!cJSON_IsObject(network)) goto parsed;
        static const char *const keys[]={"ipMode","ip","netmask","gateway","dns","hostname"};
        pet_setup_netcfg_t cfg;
        pet_setup_netcfg_default(&cfg);
        bool known=true;
        for(const cJSON *item=network->child;item&&known;item=item->next) {
            unsigned i=0;
            while(i<sizeof(keys)/sizeof(keys[0])&&(!item->string||strcmp(keys[i],item->string))) i++;
            known=i<sizeof(keys)/sizeof(keys[0]);
        }
        if(!known) goto parsed;
        if(!text_field(network,"ipMode",cfg.ip_mode,sizeof(cfg.ip_mode))||
            !text_field(network,"ip",cfg.ip,sizeof(cfg.ip))||
            !text_field(network,"netmask",cfg.netmask,sizeof(cfg.netmask))||
            !text_field(network,"gateway",cfg.gateway,sizeof(cfg.gateway))||
            !text_field(network,"dns",cfg.dns,sizeof(cfg.dns))||
            !text_field(network,"hostname",cfg.hostname,sizeof(cfg.hostname))) goto parsed;
        if(!pet_setup_netcfg_valid(&cfg)) goto parsed;
        err=pet_setup_submit_network(&cfg,expected);
    } else if(!strcmp(r->uri,"/api/finish")) {
        if(cJSON_GetArraySize(root)==1) err=pet_setup_leave();
    }
parsed:
    wipe_tree(root);cJSON_Delete(root);
done:
    setup_wipe(body,bytes+1);heap_caps_free(body);
    return err==ESP_OK?json_response(r,"202 Accepted","{\"ok\":true}"):
        json_response(r,err==ESP_ERR_TIMEOUT||err==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request",
            err==ESP_ERR_TIMEOUT?"{\"error\":\"busy\"}":err==ESP_ERR_INVALID_STATE?"{\"error\":\"revision_changed\"}":"{\"error\":\"invalid_request\"}");
}

esp_err_t setup_http_start(setup_runtime_t *s)
{
    struct ifreq interface={0};
    esp_err_t err=esp_netif_get_netif_impl_name(s->ap,interface.ifr_name);
    if(err!=ESP_OK) return err;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.if_name=&interface;config.max_uri_handlers=10;config.stack_size=8192;
    config.max_open_sockets=3;config.recv_wait_timeout=5;config.send_wait_timeout=5;config.lru_purge_enable=true;
    err=httpd_start(&s->http,&config);
    if(err!=ESP_OK) return err;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=page,.user_ctx=s},
        {.uri="/api/state",.method=HTTP_GET,.handler=status,.user_ctx=s},
        {.uri="/api/scan",.method=HTTP_GET,.handler=scan,.user_ctx=s},
        {.uri="/api/wifi",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
        {.uri="/api/services",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
        {.uri="/api/activate",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
        {.uri="/api/portal",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
        {.uri="/api/network",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
        {.uri="/api/finish",.method=HTTP_POST,.handler=mutate,.user_ctx=s},
    };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++) {
        err=httpd_register_uri_handler(s->http,&routes[i]);if(err!=ESP_OK) return err;
    }
    return ESP_OK;
}
