#include <stdio.h>
#include <stdlib.h>
#include "growth_engine.h"
#include "svc_growth/sync.h"

int main(int argc,char **argv)
{
    if(argc!=3)return 2;
    pet_growth_model_t model;pet_growth_defaults(&model);
    if(pet_growth_apply(&model,PET_GROWTH_FEED,1)!=PET_GROWTH_OK)return 3;
    pet_growth_view_t view=pet_growth_view(&model,600,false);
    view.storage_ready=true;view.time_ready=true;view.revision=1;
    char report[PET_GROWTH_SYNC_MAX];
    if(pet_growth_sync_format(argv[1],"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",1,(uint32_t)strtoul(argv[2],NULL,10),&view,report,sizeof(report))<0)return 4;
    puts(report);return 0;
}
