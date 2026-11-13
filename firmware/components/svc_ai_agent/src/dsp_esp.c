#include "svc_ai_agent/dsp.h"
#include <string.h>
#include "esp_ns.h"
#include "esp_aec.h"
#include "esp_heap_caps.h"

typedef struct {
    ns_handle_t ns;aec_handle_t *aec;
    int16_t mic[320],reference[320],stage[320],output[320];
    uint64_t reference_ms;bool reference_ready;
    pet_ai_dsp_status_t status;
} dsp_state_t;
static void algorithms_close(dsp_state_t *s)
{if(s->ns)ns_destroy(s->ns);if(s->aec)aec_destroy(s->aec);s->ns=NULL;s->aec=NULL;}
static bool algorithms_open(pet_ai_dsp_t *d,dsp_state_t *s)
{
    s->ns=ns_create(20);if(!s->ns)return false;
    if(d->request_aec) {
        s->aec=aec_create(16000,4,1,AEC_MODE_SR_LOW_COST);
        if(s->aec&&aec_get_chunksize(s->aec)!=320){aec_destroy(s->aec);s->aec=NULL;}
    }
    s->status.noise_suppression=true;s->status.aec_active=false;s->status.half_duplex_fallback=true;
    return true;
}
static pet_ai_port_result_t open_dsp(void *context)
{
    pet_ai_dsp_t *d=context;if(!d||d->state)return PET_AI_PORT_INVALID;
    dsp_state_t *s=heap_caps_aligned_alloc(16,sizeof(*s),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s)return PET_AI_PORT_FAILED;
    memset(s,0,sizeof(*s));
    if(!algorithms_open(d,s)){algorithms_close(s);heap_caps_free(s);return PET_AI_PORT_FAILED;}
    if(d->inner.open&&d->inner.open(d->inner.context)!=PET_AI_PORT_OK){algorithms_close(s);heap_caps_free(s);return PET_AI_PORT_FAILED;}
    d->state=s;return PET_AI_PORT_OK;
}
static pet_ai_port_result_t reset_dsp(void *context)
{
    pet_ai_dsp_t *d=context;if(!d||!d->state)return PET_AI_PORT_INVALID;
    dsp_state_t *s=d->state;algorithms_close(s);memset(s,0,sizeof(*s));
    if(!algorithms_open(d,s))return PET_AI_PORT_FAILED;
    return d->inner.reset?d->inner.reset(d->inner.context):PET_AI_PORT_OK;
}
static pet_ai_port_result_t close_dsp(void *context)
{
    pet_ai_dsp_t *d=context;if(!d)return PET_AI_PORT_INVALID;
    if(!d->state)return PET_AI_PORT_OK;
    pet_ai_port_result_t result=d->inner.close?d->inner.close(d->inner.context):PET_AI_PORT_OK;
    dsp_state_t *s=d->state;algorithms_close(s);memset(s,0,sizeof(*s));heap_caps_free(s);d->state=NULL;return result;
}
pet_ai_port_result_t pet_ai_dsp_reference(pet_ai_dsp_t *d,const int16_t *pcm,size_t samples,uint64_t now)
{
    if(!d||!d->state||!pcm||samples!=320)return PET_AI_PORT_INVALID;
    dsp_state_t *s=d->state;memcpy(s->reference,pcm,sizeof(s->reference));s->reference_ms=now;s->reference_ready=true;return PET_AI_PORT_OK;
}
static pet_ai_port_result_t process(void *context,const int16_t *pcm,size_t samples,uint64_t now,int16_t *out)
{
    pet_ai_dsp_t *d=context;if(!d||!d->state||!pcm||!out||samples!=320)return PET_AI_PORT_INVALID;
    dsp_state_t *s=d->state;if(!s->ns)return PET_AI_PORT_FAILED;
    memcpy(s->mic,pcm,sizeof(s->mic));
    bool reference=s->aec&&s->reference_ready&&now>=s->reference_ms&&now-s->reference_ms<=20;
    if(reference)aec_process(s->aec,s->mic,s->reference,s->stage);
    else {memcpy(s->stage,s->mic,sizeof(s->stage));if(d->request_aec)++s->status.missing_reference;}
    ns_process(s->ns,s->stage,s->output);memcpy(out,s->output,sizeof(s->output));
    s->status.aec_active=reference;s->status.half_duplex_fallback=!reference;++s->status.frames;
    s->reference_ready=false;memset(s->mic,0,sizeof(s->mic));memset(s->reference,0,sizeof(s->reference));
    memset(s->stage,0,sizeof(s->stage));memset(s->output,0,sizeof(s->output));return PET_AI_PORT_OK;
}
static pet_ai_port_result_t wake(void *context,const int16_t *pcm,size_t n,uint64_t now,bool *detected)
{pet_ai_dsp_t *d=context;return d->inner.detect_wake(d->inner.context,pcm,n,now,detected);}
static pet_ai_port_result_t vad(void *context,const int16_t *pcm,size_t n,uint64_t now,pet_ai_activity_t *activity)
{pet_ai_dsp_t *d=context;return d->inner.classify_vad(d->inner.context,pcm,n,now,activity);}
pet_ai_runtime_frontend_t pet_ai_dsp_frontend(pet_ai_dsp_t *d,pet_ai_runtime_frontend_t inner,bool request_aec)
{
    if(!d||d->state)return (pet_ai_runtime_frontend_t){0};
    d->inner=inner;d->request_aec=request_aec;
    return (pet_ai_runtime_frontend_t){.context=d,.open=open_dsp,.reset=reset_dsp,.close=close_dsp,.preprocess=process,
        .detect_wake=inner.detect_wake?wake:NULL,.classify_vad=inner.classify_vad?vad:NULL};
}
bool pet_ai_dsp_status(const pet_ai_dsp_t *d,pet_ai_dsp_status_t *out)
{if(!d||!d->state||!out)return false;*out=((dsp_state_t *)d->state)->status;return true;}
