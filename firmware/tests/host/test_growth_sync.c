#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "svc_growth/sync.h"

int main(void)
{
    const char *boot="11111111111111111111111111111111";
    pet_growth_view_t view={.revision=7,.level=2,.exp=15,.exp_to_next=128,.coins=72,
        .intimacy=10,.mood=20,.energy=80,.hunger=60,.health=100,.state=PET_GROWTH_IDLE,
        .task_day=20770,.task_progress={1,2,0},.time_ready=true,.unsaved=true,.storage_ready=true};
    char report[1536];
    int bytes=pet_growth_sync_format("growth-peer",boot,1,1800000000,&view,report,sizeof(report));
    assert(bytes>0 && bytes==(int)strlen(report));
    assert(strstr(report,"\"coins\":72") && strstr(report,"\"unsaved\":true"));
    assert(strstr(report,"\"experienceToNext\":128") && strstr(report,"\"taskProgress\":[1,2,0]"));
    assert(pet_growth_sync_format("bad/#",boot,1,1800000000,&view,report,sizeof(report))<0);
    assert(pet_growth_sync_format("growth-peer","not-a-boot",1,1800000000,&view,report,sizeof(report))<0);
    assert(pet_growth_sync_format("growth-peer",boot,0,1800000000,&view,report,sizeof(report))<0);
    assert(pet_growth_sync_format("growth-peer",boot,1,0,&view,report,sizeof(report))<0);
    assert(pet_growth_sync_format("growth-peer",boot,1,1800000000,&view,report,16)<0);
    view.level=99;view.exp=0;view.exp_to_next=80832;
    assert(pet_growth_sync_format("growth-peer",boot,2,1800000000,&view,report,sizeof(report))>0);
    view.exp=1;
    assert(pet_growth_sync_format("growth-peer",boot,2,1800000000,&view,report,sizeof(report))<0);
    puts("Read-only growth snapshot formatting and boundaries passed.");
}
