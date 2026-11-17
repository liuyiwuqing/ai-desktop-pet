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
    puts("Setup portal: defaults, bounds, effective hotspot, NVS JSON and partial update passed");
}

int main(void)
{
    defaults();bounds();effective();codec();
    return 0;
}
