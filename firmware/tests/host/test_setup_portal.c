#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "setup_policy.h"

static void defaults(void)
{
    pet_setup_portal_t portal;
    pet_setup_portal_default(&portal);
    assert(!portal.custom&&!portal.ssid[0]);
    assert(portal.password_mode==PET_SETUP_PASSWORD_AUTO&&!portal.password[0]);
    assert(!portal.hidden&&portal.channel==PET_SETUP_CHANNEL_MIN);
    assert(portal.max_clients==2&&portal.window_s==PET_SETUP_WINDOW_S);
    assert(pet_setup_portal_valid(&portal));
}

static void bounds(void)
{
    pet_setup_portal_t portal;
    pet_setup_portal_default(&portal);
    portal.custom=true;
    strcpy(portal.ssid,"Qitomo-客厅");
    assert(pet_setup_portal_valid(&portal));
    portal.ssid[0]='\n';assert(!pet_setup_portal_valid(&portal));
    /* 结构体长度由数组上限保证；超长输入在 JSON 边界覆盖。 */
    memset(portal.ssid,'s',PET_SETUP_SSID_MAX);portal.ssid[PET_SETUP_SSID_MAX]=0;
    assert(pet_setup_portal_valid(&portal));

    pet_setup_portal_default(&portal);
    portal.password_mode=PET_SETUP_PASSWORD_CUSTOM;
    strcpy(portal.password,"pass-123");
    assert(pet_setup_portal_valid(&portal));
    strcpy(portal.password,"short");assert(!pet_setup_portal_valid(&portal));
    memset(portal.password,'a',PET_SETUP_PASSWORD_MAX);portal.password[PET_SETUP_PASSWORD_MAX]=0;
    assert(pet_setup_portal_valid(&portal));
    portal.password_mode=(pet_setup_password_mode_t)9;assert(!pet_setup_portal_valid(&portal));

    pet_setup_portal_default(&portal);
    portal.channel=0;assert(!pet_setup_portal_valid(&portal));
    portal.channel=PET_SETUP_CHANNEL_MAX+1;assert(!pet_setup_portal_valid(&portal));
    portal.channel=PET_SETUP_CHANNEL_MAX;assert(pet_setup_portal_valid(&portal));
    portal.max_clients=0;assert(!pet_setup_portal_valid(&portal));
    portal.max_clients=PET_SETUP_CLIENTS_MAX+1;assert(!pet_setup_portal_valid(&portal));
    portal.max_clients=PET_SETUP_CLIENTS_MAX;assert(pet_setup_portal_valid(&portal));
    portal.window_s=PET_SETUP_WINDOW_MIN_S-60;assert(!pet_setup_portal_valid(&portal));
    portal.window_s=PET_SETUP_WINDOW_MIN_S+1;assert(!pet_setup_portal_valid(&portal));
    portal.window_s=PET_SETUP_WINDOW_MAX_S+60;assert(!pet_setup_portal_valid(&portal));
    portal.window_s=PET_SETUP_WINDOW_MAX_S;assert(pet_setup_portal_valid(&portal));
    assert(!pet_setup_portal_valid(NULL));
}

static void effective(void)
{
    uint8_t random[32];
    memset(random,0x12,sizeof(random));
    char ssid[PET_SETUP_SSID_MAX+1],password[PET_SETUP_PASSWORD_MAX+1];
    pet_setup_portal_t portal;
    pet_setup_portal_default(&portal);
    pet_setup_portal_effective(&portal,random,ssid,password);
    assert(!strcmp(ssid,"Qitomo-1212"));
    assert(strlen(password)==12);
    for(unsigned i=0;i<12;i++) assert(password[i]=='L');
    assert(!strcmp(password,"LLLLLLLLLLLL"));

    portal.custom=true;
    strcpy(portal.ssid,"Qitomo-Mypet");
    portal.password_mode=PET_SETUP_PASSWORD_CUSTOM;
    strcpy(portal.password,"my-secret-pass");
    pet_setup_portal_effective(&portal,random,ssid,password);
    assert(!strcmp(ssid,"Qitomo-Mypet")&&!strcmp(password,"my-secret-pass"));

    portal.password_mode=PET_SETUP_PASSWORD_OPEN;
    pet_setup_portal_effective(&portal,random,ssid,password);
    assert(!strcmp(ssid,"Qitomo-Mypet")&&!password[0]);

    pet_setup_portal_default(&portal);
    portal.password_mode=PET_SETUP_PASSWORD_CUSTOM;
    strcpy(portal.password,"only-fixed-pass");
    pet_setup_portal_effective(&portal,random,ssid,password);
    assert(!strcmp(ssid,"Qitomo-1212")&&!strcmp(password,"only-fixed-pass"));
}

static void codec(void)
{
    pet_setup_portal_t portal,parsed;
    pet_setup_portal_default(&portal);
    portal.custom=true;
    strcpy(portal.ssid,"Qitomo-Mypet");
    portal.password_mode=PET_SETUP_PASSWORD_CUSTOM;
    strcpy(portal.password,"my-secret-pass");
    portal.hidden=true;portal.channel=6;portal.max_clients=4;portal.window_s=3600;
    char json[512];
    assert(pet_setup_portal_format(&portal,json,sizeof(json)));
    assert(strstr(json,"\"windowMinutes\":60"));
    assert(pet_setup_portal_parse(json,strlen(json),&parsed));
    assert(parsed.custom&&!strcmp(parsed.ssid,"Qitomo-Mypet"));
    assert(parsed.password_mode==PET_SETUP_PASSWORD_CUSTOM&&!strcmp(parsed.password,"my-secret-pass"));
    assert(parsed.hidden&&parsed.channel==6&&parsed.max_clients==4&&parsed.window_s==3600);

    /* 默认策略不写入名称和口令；回读后仍为默认值。 */
    pet_setup_portal_default(&portal);portal.window_s=300;
    assert(pet_setup_portal_format(&portal,json,sizeof(json)));
    assert(!strstr(json,"\"ssid\"")&&!strstr(json,"\"password\""));
    assert(pet_setup_portal_parse(json,strlen(json),&parsed));
    assert(!parsed.custom&&parsed.password_mode==PET_SETUP_PASSWORD_AUTO&&parsed.window_s==300);

    /* 未知/重复/缺失/类型错误/旧版本/尾部垃圾都拒绝。 */
    const char *extra="{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,"
        "\"channel\":1,\"maxClients\":2,\"windowMinutes\":15,\"extra\":1}";
    assert(!pet_setup_portal_parse(extra,strlen(extra),&parsed));
    const char *const bad[]={
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15,\"hidden\":true}",
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":2,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":\"no\",\"passwordMode\":0,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":14,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":4}",
        "{\"version\":1,\"custom\":true,\"passwordMode\":1,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":true,\"ssid\":\"\",\"passwordMode\":1,\"password\":\"my-secret-pass\",\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":true,\"ssid\":\"a\",\"passwordMode\":1,\"password\":\"short\",\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":1.5,\"maxClients\":2,\"windowMinutes\":15}",
        "{\"version\":1,\"custom\":false,\"passwordMode\":0,\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15} ",
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        assert(!pet_setup_portal_parse(bad[i],strlen(bad[i]),&parsed));
    char space[520];
    memset(space,' ',sizeof(space));
    assert(!pet_setup_portal_parse(space,sizeof(space),&parsed));

    /* 页面不回显口令：允许留空，由调用方补齐后再次校验。 */
    const char *partial="{\"version\":1,\"custom\":true,\"passwordMode\":1,\"hidden\":false,"
        "\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}";
    assert(!pet_setup_portal_parse(partial,strlen(partial),&parsed));
    assert(pet_setup_portal_parse_partial(partial,strlen(partial),&parsed));
    assert(parsed.custom&&!parsed.ssid[0]&&!parsed.password[0]);
    strcpy(parsed.ssid,"Qitomo-Mypet");strcpy(parsed.password,"my-secret-pass");
    assert(pet_setup_portal_valid(&parsed));
    /* 开放热点不应夹带口令，parse 会清掉跟随的非活动字段。 */
    const char *open="{\"version\":1,\"custom\":false,\"passwordMode\":2,\"password\":\"ignored-pass\","
        "\"hidden\":false,\"channel\":1,\"maxClients\":2,\"windowMinutes\":15}";
    assert(pet_setup_portal_parse(open,strlen(open),&parsed)&&!parsed.password[0]);
    assert(!strcmp(pet_setup_state_name(PET_SETUP_PORTAL_SAVED),"portal_saved"));
    assert(!strcmp(pet_setup_state_name(PET_SETUP_NETWORK_SAVED),"network_saved"));
    puts("Setup portal: defaults, bounds, effective hotspot, NVS JSON, partial update, address rules and status formatting passed");
}

static void netcfg(void)
{
    pet_setup_netcfg_t cfg;
    pet_setup_netcfg_default(&cfg);
    assert(!strcmp(cfg.ip_mode,"dhcp")&&!cfg.ip[0]&&!cfg.hostname[0]);
    assert(pet_setup_netcfg_valid(&cfg));

    /* 自动获取时不能残留静态字段，否则“改回 DHCP”看起来没生效。 */
    strcpy(cfg.ip,"192.168.1.50");
    assert(!pet_setup_netcfg_valid(&cfg));
    pet_setup_netcfg_default(&cfg);

    strcpy(cfg.ip_mode,"auto");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.ip_mode,"");
    assert(!pet_setup_netcfg_valid(&cfg));

    strcpy(cfg.ip_mode,"static");
    strcpy(cfg.ip,"192.168.1.50");
    strcpy(cfg.netmask,"255.255.255.0");
    strcpy(cfg.gateway,"192.168.1.1");
    assert(pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.dns,"192.168.1.1");
    strcpy(cfg.hostname,"qitomo-pet");
    assert(pet_setup_netcfg_valid(&cfg));

    strcpy(cfg.ip,"192.168.1");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.ip,"192.168.01.50");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.ip,"192.168.1.256");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.ip,"192.168.1.50");
    strcpy(cfg.netmask,"255.0.255.0");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.netmask,"0.0.0.0");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.netmask,"255.255.255.0");
    strcpy(cfg.gateway,"10.0.0.1");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.gateway,"192.168.1.1");
    strcpy(cfg.dns,"not-an-ip");
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.dns,"");
    cfg.netmask[0]=0;
    assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.netmask,"255.255.255.0");

    strcpy(cfg.hostname,"-bad");assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.hostname,"bad-");assert(!pet_setup_netcfg_valid(&cfg));
    strcpy(cfg.hostname,"bad host");assert(!pet_setup_netcfg_valid(&cfg));
    /* 结构体长度已由数组上限保证；超长主机名在凭据/JSON 边界拒绝。 */
    memset(cfg.hostname,'h',PET_NETCFG_HOSTNAME_MAX);cfg.hostname[PET_NETCFG_HOSTNAME_MAX]=0;
    assert(pet_setup_netcfg_valid(&cfg));
    assert(!pet_setup_netcfg_valid(NULL));
}

/* 状态页展示：只暴露可公开字段，且脱敏规则不依赖设备。 */
static void status_helpers(void)
{
    assert(!strcmp(pet_setup_ap_security(true),"开放网络"));
    assert(!strcmp(pet_setup_ap_security(false),"WPA2"));

    char host[64]={0};bool tls=false;
    assert(pet_setup_broker_parse("mqtts://mqtt.example.com:8883",&tls,host,sizeof(host)));
    assert(tls&&!strcmp(host,"mqtt.example.com:8883"));
    assert(pet_setup_broker_parse("mqtt://10.0.0.7:1883",&tls,host,sizeof(host)));
    assert(!tls&&!strcmp(host,"10.0.0.7:1883"));
    /* 账号、口令与路径一律丢掉，避免把凭据带到页面上。 */
    assert(pet_setup_broker_parse("mqtts://user:secret@broker.local:8883/mqtt",&tls,host,sizeof(host)));
    assert(tls&&!strcmp(host,"broker.local:8883"));
    assert(pet_setup_broker_parse("mqtts://@broker.local",&tls,host,sizeof(host)));
    assert(!strcmp(host,"broker.local"));
    /* 没有协议头时按原样当主机名，仍不视为 TLS。 */
    assert(pet_setup_broker_parse("broker.local:8883",&tls,host,sizeof(host)));
    assert(!tls&&!strcmp(host,"broker.local:8883"));
    assert(!pet_setup_broker_parse("",&tls,host,sizeof(host)));
    assert(!pet_setup_broker_parse("mqtts://",&tls,host,sizeof(host)));
    assert(!pet_setup_broker_parse(NULL,&tls,host,sizeof(host)));
    char tiny[8]={0};
    assert(!pet_setup_broker_parse("mqtts://broker.example.com:8883",&tls,tiny,sizeof(tiny)));
    assert(pet_setup_broker_parse("mqtts://broker.local:8883",NULL,host,sizeof(host)));

    char account[PET_SETUP_ACCOUNT_MAX]={0};
    assert(pet_setup_mask_account("dodo-1",account,sizeof(account)));
    assert(!strcmp(account,"d***"));
    assert(pet_setup_mask_account("名字很长的账号",account,sizeof(account)));
    assert(!strcmp(account,"名***")); /* 只保留首个 UTF-8 字符，不截断成乱码。 */
    assert(pet_setup_mask_account("a",account,sizeof(account)));
    assert(!strcmp(account,"a***"));
    assert(!pet_setup_mask_account("",account,sizeof(account)));
    assert(!pet_setup_mask_account(NULL,account,sizeof(account)));
    assert(!pet_setup_mask_account("dodo",account,4));
}

int main(void)
{
    defaults();bounds();effective();codec();netcfg();status_helpers();
    return 0;
}
