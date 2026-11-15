#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "setup_policy.h"

int main(void)
{
    const char *token="0123456789abcdef0123456789abcdef";
    assert(pet_setup_request_authorized(PET_SETUP_HOST,NULL,NULL,NULL,false));
    assert(!pet_setup_request_authorized("attacker.test",NULL,NULL,NULL,false));
    assert(!pet_setup_request_authorized(PET_SETUP_HOST,"http://attacker.test",NULL,NULL,false));
    assert(pet_setup_request_authorized(PET_SETUP_HOST,PET_SETUP_ORIGIN,token,token,true));
    assert(!pet_setup_request_authorized(PET_SETUP_HOST,NULL,token,token,true));
    assert(!pet_setup_request_authorized(PET_SETUP_HOST,PET_SETUP_ORIGIN,"wrong",token,true));
    assert(!pet_setup_request_authorized(PET_SETUP_HOST,PET_SETUP_ORIGIN,token,"short",true));
    assert(pet_setup_wifi_valid("家庭 Wi-Fi","dummy-pass"));
    assert(pet_setup_wifi_valid("open",""));
    assert(!pet_setup_wifi_valid("","dummy-pass"));
    assert(!pet_setup_wifi_valid("home","short"));
    assert(!pet_setup_wifi_valid("home\n","dummy-pass"));
    char max_ssid[34],max_password[66];
    memset(max_ssid,'s',32);max_ssid[32]=0;
    memset(max_password,'a',64);max_password[64]=0;
    assert(pet_setup_wifi_valid(max_ssid,max_password));
    max_password[0]='z';assert(!pet_setup_wifi_valid(max_ssid,max_password));
    max_password[63]=0;assert(pet_setup_wifi_valid(max_ssid,max_password));
    max_ssid[32]='s';max_ssid[33]=0;assert(!pet_setup_wifi_valid(max_ssid,max_password));
    assert(!strcmp(pet_setup_state_name(PET_SETUP_WIFI_SAVED),"wifi_saved"));
    puts("Setup policy: AP host, origin/session, Wi-Fi bounds and open/secured networks passed");
}
