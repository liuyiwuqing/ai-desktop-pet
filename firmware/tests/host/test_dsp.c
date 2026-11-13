#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "svc_ai_agent/dsp.h"
#include "esp_ns.h"
#include "esp_aec.h"
/* Xtensa DSP 与堆是系统边界，验证参考接线/生命周期，不伪装消噪效果。 */
static unsigned ns_live,aec_live,aec_frames;
void *heap_caps_aligned_alloc(size_t a,size_t n,unsigned caps) {(void)caps;void *p=NULL;return posix_memalign(&p,a,n)?NULL:p;}
void heap_caps_free(void *p){free(p);}
ns_handle_t ns_create(int ms){assert(ms==20);++ns_live;return malloc(1);}
void ns_process(ns_handle_t h,int16_t *in,int16_t *out){assert(h);memcpy(out,in,640);}
void ns_destroy(ns_handle_t h){if(h){--ns_live;free(h);}}
aec_handle_t *aec_create(int rate,int length,int channels,aec_mode_t mode)
{assert(rate==16000&&length==4&&channels==1&&mode==AEC_MODE_SR_LOW_COST);++aec_live;return calloc(1,sizeof(aec_handle_t));}
int aec_get_chunksize(const aec_handle_t *h){assert(h);return 320;}
void aec_process(const aec_handle_t *h,int16_t *in,int16_t *ref,int16_t *out)
{assert(h&&ref[0]==77);++aec_frames;memcpy(out,in,640);}
void aec_destroy(aec_handle_t *h){if(h){--aec_live;free(h);}}
int main(void)
{
    pet_ai_dsp_t dsp={0};pet_ai_runtime_frontend_t f=pet_ai_dsp_frontend(&dsp,(pet_ai_runtime_frontend_t){0},true);
    int16_t mic[320]={25},ref[320]={77},out[320];pet_ai_dsp_status_t status;
    assert(f.open(f.context)==PET_AI_PORT_OK);
    assert(f.preprocess(f.context,mic,320,100,out)==PET_AI_PORT_OK&&out[0]==25&&aec_frames==0);
    assert(pet_ai_dsp_status(&dsp,&status)&&status.half_duplex_fallback&&!status.aec_active);
    assert(pet_ai_dsp_reference(&dsp,ref,320,100)==PET_AI_PORT_OK);
    assert(f.preprocess(f.context,mic,320,110,out)==PET_AI_PORT_OK&&aec_frames==1);
    assert(pet_ai_dsp_reference(&dsp,ref,320,110)==PET_AI_PORT_OK);
    assert(f.preprocess(f.context,mic,320,150,out)==PET_AI_PORT_OK&&aec_frames==1);
    assert(f.reset(f.context)==PET_AI_PORT_OK);
    assert(f.preprocess(f.context,mic,320,160,out)==PET_AI_PORT_OK&&aec_frames==1);
    assert(f.close(f.context)==PET_AI_PORT_OK&&ns_live==0&&aec_live==0);
    puts("DSP: fresh reference, half-duplex fallback, reset and resource lifetime passed (vendor boundary)");
}
