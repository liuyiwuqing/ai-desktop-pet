#include "setup_policy.h"
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
        "waiting_bind","voice_saved","services_saved","failed","leaving"};
    return (unsigned)state<sizeof(names)/sizeof(names[0])?names[state]:"failed";
}
