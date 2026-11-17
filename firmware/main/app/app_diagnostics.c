#include "app_diagnostics.h"
#include "app_backup.h"
#include "app_recovery.h"
#include "sdkconfig.h"
#if CONFIG_PET_SETUP_PORTAL_ENABLED
#include "svc_setup/api.h"
#endif
#include <string.h>
#include <stdio.h>
#include "svc_config/provision.h"
#include "svc_config/api.h"
#include "pet/nexus.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "pet/storage.h"
#include "pet/event_bus.h"
#include "cJSON.h"
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
#include "esp_core_dump.h"
#endif
#if CONFIG_PET_MODULE_UI_ENABLED
#include "ui_lvgl/api.h"
#endif
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
#include "svc_ai_agent/api.h"
#endif
#if CONFIG_PET_AI_XIAOZHI_WSS_ENABLED
#include "svc_ai_agent/wss.h"
#endif
#include "svc_message_hub/api.h"
#include "svc_net/api.h"
#include "svc_ota/api.h"
#if CONFIG_PET_EXT_SERVO
#include "pet_ext/servo_service.h"
#endif

esp_err_t app_summary_json(const char *device,uint32_t epoch,char *out,size_t capacity)
{
    if(!pet_nexus_device_id_valid(device)||!epoch||!out||capacity<512)return ESP_ERR_INVALID_ARG;
    pet_config_snapshot_t settings;
    if(pet_config_snapshot(&settings)!=ESP_OK)return ESP_ERR_INVALID_STATE;
    char config[PET_CONFIG_JSON_MAX];size_t config_bytes=0;
    esp_err_t formatted=pet_config_format_json(&settings.config,config,sizeof(config),&config_bytes);
    if(formatted!=ESP_OK)return formatted;
    cJSON *root=cJSON_CreateObject();if(!root)return ESP_ERR_NO_MEM;
    if(!cJSON_AddRawToObject(root,"config",config)) { cJSON_Delete(root);return ESP_ERR_NO_MEM; }
    cJSON_AddNumberToObject(root,"schemaVersion",1);cJSON_AddStringToObject(root,"deviceId",device);
    cJSON_AddNumberToObject(root,"observedAt",epoch);cJSON_AddStringToObject(root,"firmwareVersion",esp_app_get_description()->version);
    cJSON_AddNumberToObject(root,"configRevision",settings.revision);cJSON_AddBoolToObject(root,"configWritable",settings.writable);
    bool muted=settings.config.muted,dnd=pet_config_dnd_active(&settings.config,epoch,true);
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
    pet_ai_agent_snapshot_t ai;
    if(pet_ai_agent_snapshot(&ai)==ESP_OK) {
        muted=ai.policy.muted;dnd=ai.policy.dnd;
        cJSON_AddNumberToObject(root,"audioUnderruns",ai.playback_underruns);
        cJSON_AddNumberToObject(root,"inferenceMaxMs",ai.inference_max_ms);
        cJSON_AddBoolToObject(root,"audioFaulted",ai.faulted);
    }
#endif
    cJSON_AddBoolToObject(root,"muted",muted);cJSON_AddBoolToObject(root,"dnd",dnd);
    cJSON_AddNumberToObject(root,"volume",settings.config.volume);cJSON_AddNumberToObject(root,"brightness",settings.config.brightness);
    cJSON_AddNumberToObject(root,"unread",pet_message_hub_depth());
    cJSON_AddNumberToObject(root,"freeHeap",esp_get_free_heap_size());cJSON_AddNumberToObject(root,"minFreeHeap",esp_get_minimum_free_heap_size());
#if CONFIG_PET_MODULE_NET_ENABLED
    /* 只上报链路标识与质量，不含口令或 Broker 凭据；取不到的字段一律省略。 */
    char wifi_ip[16],wifi_ssid[33],wifi_bssid[18];int wifi_rssi,wifi_channel,wifi_width,wifi_reason;uint32_t wifi_disconnected_at;
    if(pet_net_local_ip(wifi_ip,sizeof(wifi_ip))==ESP_OK)cJSON_AddStringToObject(root,"wifiIp",wifi_ip);
    if(pet_net_rssi(&wifi_rssi)==ESP_OK)cJSON_AddNumberToObject(root,"wifiRssi",wifi_rssi);
    if(pet_net_ssid(wifi_ssid,sizeof(wifi_ssid))==ESP_OK)cJSON_AddStringToObject(root,"wifiSsid",wifi_ssid);
    if(pet_net_channel(&wifi_channel)==ESP_OK)cJSON_AddNumberToObject(root,"wifiChannel",wifi_channel);
    if(pet_net_bssid(wifi_bssid,sizeof(wifi_bssid))==ESP_OK)cJSON_AddStringToObject(root,"wifiBssid",wifi_bssid);
    if(pet_net_channel_width(&wifi_width)==ESP_OK)cJSON_AddNumberToObject(root,"wifiChannelWidth",wifi_width);
    if(pet_net_last_disconnect(&wifi_reason,&wifi_disconnected_at)==ESP_OK){
        cJSON_AddNumberToObject(root,"wifiDisconnectReason",wifi_reason);
        if(wifi_disconnected_at!=0)cJSON_AddNumberToObject(root,"wifiDisconnectedAt",wifi_disconnected_at);
    }
#endif
#if CONFIG_PET_MODULE_UI_ENABLED
    pet_ui_metrics_t ui;pet_ui_metrics(&ui);cJSON_AddNumberToObject(root,"displayMaxMs",ui.max_frame_ms);
#endif
    bool ok=cJSON_PrintPreallocated(root,out,(int)capacity,false);cJSON_Delete(root);
    return ok?ESP_OK:ESP_ERR_INVALID_SIZE;
}

esp_err_t app_diagnostics_json(char *out,size_t capacity)
{
    if (!out||capacity<128) return ESP_ERR_INVALID_ARG;
    cJSON *root=cJSON_CreateObject();if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddBoolToObject(root,"ok",true);cJSON *r=cJSON_AddObjectToObject(root,"diagnostics");
    cJSON_AddNumberToObject(r,"uptimeMs",(double)(esp_timer_get_time()/1000));
    cJSON_AddNumberToObject(r,"freeHeap",esp_get_free_heap_size());
    cJSON_AddNumberToObject(r,"minFreeHeap",esp_get_minimum_free_heap_size());
    cJSON_AddNumberToObject(r,"internalFree",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(r,"internalLargest",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    cJSON_AddNumberToObject(r,"dmaFree",heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r,"dmaLargest",heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA));
    cJSON_AddNumberToObject(r,"psramFree",heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
#if CONFIG_PET_SETUP_PORTAL_ENABLED
    pet_setup_snapshot_t setup;
    if(pet_setup_snapshot(&setup)==ESP_OK) {
        cJSON *local=cJSON_AddObjectToObject(r,"setup");
        cJSON_AddNumberToObject(local,"state",setup.state);
        cJSON_AddNumberToObject(local,"code",setup.error);
        cJSON_AddNumberToObject(local,"remainingSeconds",setup.remaining_s);
        cJSON_AddNumberToObject(local,"revision",setup.revision);
        cJSON_AddBoolToObject(local,"wifiSaved",setup.wifi_saved);
        cJSON_AddBoolToObject(local,"voiceSaved",setup.voice_saved);
        /* 热点偏好只输出策略与数量，名称和口令不进入诊断/日志。 */
        cJSON_AddBoolToObject(local,"portalCustom",setup.portal.custom);
        cJSON_AddNumberToObject(local,"portalPasswordMode",setup.portal.password_mode);
        cJSON_AddBoolToObject(local,"portalHidden",setup.portal.hidden);
        cJSON_AddNumberToObject(local,"portalChannel",setup.portal.channel);
        cJSON_AddNumberToObject(local,"portalClients",setup.portal.max_clients);
        cJSON_AddNumberToObject(local,"portalWindow",setup.portal.window_s);
        /* 热点口令、会话 nonce、Wi-Fi 和供应商凭据不进入诊断/日志。 */
        volatile unsigned char *secret=(volatile unsigned char *)&setup;
        for(size_t i=0;i<sizeof(setup);i++) secret[i]=0;
    }
#endif
    cJSON_AddNumberToObject(r,"eventsDropped",pet_event_bus_dropped());
    pet_storage_metrics_t nvs;pet_storage_metrics(&nvs);
    cJSON_AddNumberToObject(r,"nvsWrites",nvs.write_attempts);cJSON_AddNumberToObject(r,"nvsFailures",nvs.write_failures);
    cJSON_AddNumberToObject(r,"unread",pet_message_hub_depth());
    cJSON_AddNumberToObject(r,"pendingAcks",pet_message_hub_pending_acks());
    cJSON_AddStringToObject(r,"firmwareIdentity",pet_ota_identity_marker());
    char elf_hash[65];
    const uint8_t *elf_sha=esp_app_get_description()->app_elf_sha256;
    for(unsigned i=0;i<32;++i)snprintf(elf_hash+i*2,3,"%02x",elf_sha[i]);
    cJSON_AddStringToObject(r,"appElfSha256",elf_hash);
    app_recovery_snapshot_t recovery;app_recovery_snapshot(&recovery);
    cJSON *boot=cJSON_AddObjectToObject(r,"recovery");
    cJSON_AddBoolToObject(boot,"enabled",recovery.enabled);cJSON_AddBoolToObject(boot,"active",recovery.active);
    cJSON_AddBoolToObject(boot,"healthy",recovery.healthy);cJSON_AddNumberToObject(boot,"failures",recovery.failures);
    cJSON_AddBoolToObject(boot,"displayAttempted",recovery.ui_attempted);
    cJSON_AddNumberToObject(boot,"resetReason",recovery.reset_reason);
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    size_t dump_address=0,dump_bytes=0;
    esp_err_t dump_error=esp_core_dump_image_get(&dump_address,&dump_bytes);
    cJSON *dump=cJSON_AddObjectToObject(r,"coreDump");
    cJSON_AddBoolToObject(dump,"present",dump_error==ESP_OK);cJSON_AddNumberToObject(dump,"code",dump_error);
    if(dump_error==ESP_OK)cJSON_AddNumberToObject(dump,"bytes",dump_bytes);
#endif
#if CONFIG_PET_OTA_ENABLED
    pet_ota_snapshot_t ota;
    if(pet_ota_snapshot(&ota)==ESP_OK) {
        cJSON *item=cJSON_AddObjectToObject(r,"ota");
        cJSON_AddNumberToObject(item,"state",ota.state);cJSON_AddNumberToObject(item,"error",ota.error);
        cJSON_AddNumberToObject(item,"writtenBytes",ota.written_bytes);cJSON_AddNumberToObject(item,"releaseSequence",ota.release_sequence);
    }
#endif
#if CONFIG_PET_MODULE_UI_ENABLED
    pet_ui_metrics_t ui;pet_ui_metrics(&ui);
    cJSON_AddNumberToObject(r,"displayFrames",ui.frames);cJSON_AddNumberToObject(r,"displayFailed",ui.failed_frames);
    cJSON_AddNumberToObject(r,"displayMaxMs",ui.max_frame_ms);
#endif
#if CONFIG_PET_MODULE_AI_ENABLED && CONFIG_PET_MODULE_AUDIO_ENABLED
    pet_ai_agent_snapshot_t ai;
    if (pet_ai_agent_snapshot(&ai)==ESP_OK) {
        cJSON_AddBoolToObject(r,"aiReady",ai.ready);cJSON_AddBoolToObject(r,"audioFaulted",ai.faulted);
        cJSON_AddNumberToObject(r,"audioError",ai.last_error);cJSON_AddNumberToObject(r,"cleanupError",ai.cleanup_error);
        cJSON_AddNumberToObject(r,"audioUnderruns",ai.playback_underruns);
        cJSON_AddNumberToObject(r,"inferenceCount",ai.inference_count);
        cJSON_AddNumberToObject(r,"inferenceMaxMs",ai.inference_max_ms);
        cJSON_AddNumberToObject(r,"audioStackFree",ai.owner_stack_free);
        cJSON_AddNumberToObject(r,"aiState",ai.view.state);
        cJSON_AddNumberToObject(r,"aiReason",ai.view.reason);
        cJSON_AddNumberToObject(r,"aiTurn",ai.view.turn_id);
        cJSON_AddNumberToObject(r,"audioSubmittedSamples",(double)ai.submitted_samples);
    }
#endif
#if CONFIG_PET_AI_XIAOZHI_WSS_ENABLED
    pet_xz_wss_snapshot_t transport;
    if(pet_xz_wss_snapshot(&transport)==ESP_OK) {
        cJSON *voice=cJSON_AddObjectToObject(r,"voiceTransport");
        cJSON_AddBoolToObject(voice,"ready",transport.ready);
        cJSON_AddBoolToObject(voice,"networkReady",transport.network_ready);
        cJSON_AddBoolToObject(voice,"faulted",transport.faulted);
        cJSON_AddNumberToObject(voice,"epoch",transport.epoch);
        cJSON_AddNumberToObject(voice,"turnId",transport.turn_id);
        cJSON_AddNumberToObject(voice,"error",transport.last_error);
        cJSON_AddNumberToObject(voice,"cleanupError",transport.cleanup_error);
        cJSON_AddNumberToObject(voice,"sentPackets",transport.sent_packets);
        cJSON_AddNumberToObject(voice,"receivedPackets",transport.received_packets);
        cJSON_AddNumberToObject(voice,"mcpRequests",transport.mcp_received);
        cJSON_AddNumberToObject(voice,"mcpResponses",transport.mcp_sent);
        cJSON_AddNumberToObject(voice,"mcpFailures",transport.mcp_failed);
        cJSON_AddNumberToObject(voice,"ownerStackFree",transport.owner_stack_free);
    }
#endif
    bool ok=cJSON_PrintPreallocated(root,out,(int)capacity,false);cJSON_Delete(root);
    return ok?ESP_OK:ESP_ERR_INVALID_SIZE;
}

esp_err_t app_maintenance_command(const char *line,size_t bytes,char *out,size_t capacity)
{
    if (!line||!out||capacity<128) return ESP_ERR_INVALID_ARG;
#if CONFIG_PET_RECOVERY_ENABLED
    if(bytes==18&&!memcmp(line,"PET_RECOVERY_CLEAR",18)) {
        app_recovery_clear();snprintf(out,capacity,"{\"ok\":true,\"restartRequired\":true}");return ESP_OK;
    }
    if(app_recovery_active()&&((bytes>=8&&!memcmp(line,"PET_OTA_",8))||
       (bytes>=10&&!memcmp(line,"PET_SERVO_",10)))) {
        snprintf(out,capacity,"{\"ok\":false,\"code\":%d}",ESP_ERR_INVALID_STATE);return ESP_ERR_INVALID_STATE;
    }
#endif
#if CONFIG_PET_OTA_ENABLED
    if(bytes>14&&bytes<=78&&!memcmp(line,"PET_OTA_APPLY ",14)) {
        char release[65]={0};memcpy(release,line+14,bytes-14);
        esp_err_t error=pet_ota_apply(release);
        snprintf(out,capacity,"{\"ok\":%s,\"code\":%d,\"queued\":%s}",error==ESP_OK?"true":"false",error,error==ESP_OK?"true":"false");return error;
    }
    if(bytes==14&&!memcmp(line,"PET_OTA_REBOOT",14)) {
        esp_err_t error=pet_ota_reboot();snprintf(out,capacity,"{\"ok\":%s,\"code\":%d}",error==ESP_OK?"true":"false",error);return error;
    }
#endif
#if CONFIG_PET_EXT_SERVO
    if((bytes==30&&!memcmp(line,"PET_SERVO_ARM CALIBRATED_POWER",30))||
        (bytes==15&&!memcmp(line,"PET_SERVO_ESTOP",15))) {
        esp_err_t error=bytes==30?pet_servo_service_arm():pet_servo_service_stop(true);
        snprintf(out,capacity,"{\"ok\":%s,\"code\":%d}",error==ESP_OK?"true":"false",error);return error;
    }
#endif
    if (bytes==15&&!memcmp(line,"PET_DIAGNOSTICS",15)) {
        esp_err_t error=app_diagnostics_json(out,capacity);
        if (error!=ESP_OK) snprintf(out,capacity,"{\"ok\":false,\"code\":%d}",error);
        return error;
    }
#if CONFIG_PET_MODULE_GROWTH_ENABLED
    if (bytes>=11&&!memcmp(line,"PET_GROWTH_",11)) return app_backup_command(line,bytes,out,capacity);
#endif
    return pet_config_command(line,bytes,out,capacity);
}
