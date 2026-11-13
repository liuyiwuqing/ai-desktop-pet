#include "svc_ota/transfer.h"
bool pet_ota_transfer(const pet_ota_manifest_t *m,const pet_ota_transfer_port_t *p,uint64_t deadline,uint8_t *buffer,size_t capacity)
{
    if(!m||!p||!p->now_ms||!p->begin||!p->read||!p->write||!p->finish||!p->verify||!p->reserve||!p->commit||!p->cleanup||
        !buffer||!capacity||capacity>4096||m->image_size<288||m->image_size>0x400000||!m->release_sequence)return false;
    bool ok=false;uint32_t written=0;
    if(p->now_ms(p->context)>=deadline||!p->begin(p->context,m))goto done;
    while(written<m->image_size) {
        size_t count=m->image_size-written,got=0;if(count>capacity)count=capacity;
        if(p->now_ms(p->context)>=deadline||!p->read(p->context,buffer,count,&got)||!got||got>count||
            p->now_ms(p->context)>=deadline||!p->write(p->context,buffer,got))goto done;
        written+=(uint32_t)got;if(p->progress)p->progress(p->context,written);
    }
    if(p->now_ms(p->context)>=deadline||!p->finish(p->context)||!p->verify(p->context,m)||
        p->now_ms(p->context)>=deadline||!p->reserve(p->context,m->release_sequence)||p->now_ms(p->context)>=deadline)goto done;
    ok=p->commit(p->context);
done:
    p->cleanup(p->context);return ok;
}
