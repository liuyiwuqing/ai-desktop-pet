#include "svc_ota/transfer.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct {unsigned fail,stage,commits,cleanups,verified,reserved;size_t offset,written;uint64_t now;} boundary_t;
static uint64_t now(void *p){return ((boundary_t *)p)->now;}
static bool next(boundary_t *b){return ++b->stage!=b->fail;}
static bool begin(void *p,const pet_ota_manifest_t *m){assert(m->image_size==1024);return next(p);}
static bool read(void *p,uint8_t *out,size_t cap,size_t *got)
{
    boundary_t *b=p;if(!next(b))return false;
    *got=cap>127?127:cap;memset(out,42,*got);b->offset+=*got;
    if(b->fail==100)*got=0; /* 截断；不能当成完整镜像。 */
    if(b->fail==101)b->now=1000; /* 每次调用未超时，整个事务已过期。 */
    if(b->fail==102)*got=cap+1;
    return true;
}
static bool write(void *p,const uint8_t *data,size_t count)
{boundary_t *b=p;if(!next(b))return false;assert(data[0]==42);b->written+=count;return true;}
static bool finish(void *p){boundary_t *b=p;assert(b->written==1024);return next(b);}
static bool verify(void *p,const pet_ota_manifest_t *m)
{boundary_t *b=p;assert(m->image_size==1024);++b->verified;return next(b);}
static bool reserve(void *p,uint32_t sequence)
{boundary_t *b=p;assert(b->verified&&sequence==3);++b->reserved;return next(b);}
static bool commit(void *p){boundary_t *b=p;assert(b->verified&&b->reserved&&b->written==1024);++b->commits;return next(b);}
static void cleanup(void *p){++((boundary_t *)p)->cleanups;}
int main(void)
{
    pet_ota_manifest_t manifest={.image_size=1024,.release_sequence=3};uint8_t buffer[256];
    boundary_t b={0};pet_ota_transfer_port_t port={&b,now,begin,read,write,finish,verify,reserve,commit,cleanup,NULL};
    assert(pet_ota_transfer(&manifest,&port,1000,buffer,sizeof(buffer))&&b.commits==1&&b.cleanups==1);
    unsigned stages=b.stage;
    for(unsigned i=1;i<=stages;++i) {
        b=(boundary_t){.fail=i};assert(!pet_ota_transfer(&manifest,&port,1000,buffer,sizeof(buffer))&&b.cleanups==1);
        assert(b.commits==(i==stages?1u:0u));
    }
    for(unsigned i=100;i<=102;++i){b=(boundary_t){.fail=i};assert(!pet_ota_transfer(&manifest,&port,1000,buffer,sizeof(buffer))&&b.commits==0&&b.cleanups==1);}
    puts("OTA transfer: complete image and verified durable sequence required before boot selection; all failure paths clean up");
}
