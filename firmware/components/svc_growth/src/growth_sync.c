#include "svc_growth/sync.h"
#include "pet/nexus.h"
#include <inttypes.h>
#include <stdio.h>

static bool boot_valid(const char *boot)
{
    if(!boot)return false;
    for(unsigned i=0;i<32;i++)if(!((boot[i]>='0'&&boot[i]<='9')||(boot[i]>='a'&&boot[i]<='f')))return false;
    return boot[32]=='\0';
}
static bool view_valid(const pet_growth_view_t *v)
{
    if(!v||v->level<1||v->level>99||v->exp_to_next!=pet_growth_exp_to_next(v->level)||
        v->exp>=v->exp_to_next||(v->level==99&&v->exp)||v->coins>999999||v->intimacy>100||
        v->mood< -100||v->mood>100||v->energy>100||v->hunger>100||v->health>100||
        (unsigned)v->state>PET_GROWTH_SLEEPING||v->environment>PET_GROWTH_ENV_HUMID||
        v->task_claimed>7||v->achievements>7)return false;
    for(unsigned i=0;i<3;i++) {
        unsigned max=i==1?3:1;
        if(v->task_progress[i]>max||((v->task_claimed&(1u<<i))&&v->task_progress[i]!=max))return false;
    }
    return true;
}
static const char *boolean(bool value){return value?"true":"false";}
int pet_growth_sync_format(const char *device,const char *boot,uint32_t sequence,
    uint32_t observed,const pet_growth_view_t *v,char *out,size_t capacity)
{
    if(!out||!capacity||!pet_nexus_device_id_valid(device)||!boot_valid(boot)||!sequence||!observed||!view_valid(v))return -1;
    int bytes=snprintf(out,capacity,
        "{\"schemaVersion\":1,\"deviceId\":\"%s\",\"bootId\":\"%s\",\"reportSequence\":%" PRIu32 ",\"observedAt\":%" PRIu32
        ",\"growthSchema\":4,\"growth\":{\"revision\":%" PRIu32 ",\"level\":%u,\"experience\":%" PRIu32 ",\"experienceToNext\":%" PRIu32
        ",\"coins\":%" PRIu32 ",\"intimacy\":%u,\"mood\":%d,\"energy\":%u,\"hunger\":%u,\"health\":%u,\"state\":\"%s\",\"sleeping\":%s,"
        "\"taskDay\":%" PRIu32 ",\"taskProgress\":[%u,%u,%u],\"taskClaimed\":%u,\"achievements\":%u,"
        "\"timeReady\":%s,\"unsaved\":%s,\"storageReady\":%s,\"saveRecovered\":%s,\"environment\":\"%s\"}}",
        device,boot,sequence,observed,v->revision,(unsigned)v->level,v->exp,v->exp_to_next,v->coins,(unsigned)v->intimacy,(int)v->mood,
        (unsigned)v->energy,(unsigned)v->hunger,(unsigned)v->health,pet_growth_state_name(v->state),boolean(v->sleeping),v->task_day,
        (unsigned)v->task_progress[0],(unsigned)v->task_progress[1],(unsigned)v->task_progress[2],(unsigned)v->task_claimed,(unsigned)v->achievements,
        boolean(v->time_ready),boolean(v->unsaved),boolean(v->storage_ready),boolean(v->save_recovered),pet_growth_environment_name(v->environment));
    if(bytes<0||(size_t)bytes>=capacity){out[0]='\0';return -1;}
    return bytes;
}
