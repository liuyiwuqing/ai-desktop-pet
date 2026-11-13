#pragma once
#include "svc_ai_agent/runtime.h"
/* 音频拥有者串行使用。使用 ESP-SR 的 NS/AEC 算法；未创建资源前不能引用。
 * 主链路仍为半双工，AEC 只有存在真实、同采样率且新鲜的参考帧时才运行。 */
typedef struct {
    void *state;
    pet_ai_runtime_frontend_t inner;
    bool request_aec;
} pet_ai_dsp_t;
typedef struct {bool noise_suppression,aec_active,half_duplex_fallback;uint32_t frames,missing_reference;} pet_ai_dsp_status_t;
pet_ai_runtime_frontend_t pet_ai_dsp_frontend(pet_ai_dsp_t *dsp,pet_ai_runtime_frontend_t inner,bool request_aec);
pet_ai_port_result_t pet_ai_dsp_reference(pet_ai_dsp_t *dsp,const int16_t *reference,size_t samples,uint64_t now_ms);
bool pet_ai_dsp_status(const pet_ai_dsp_t *dsp,pet_ai_dsp_status_t *out);
