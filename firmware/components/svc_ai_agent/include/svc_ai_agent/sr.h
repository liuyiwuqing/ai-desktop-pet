#pragma once

#include "svc_ai_agent/runtime.h"

/* 零初始化，实例寿命覆盖 agent；只有音频拥有者可使用，不能跨任务读 state。
 * 固定 wn9_xiaoaitongxue + vadnet1_medium、16 kHz/mono/s16；无 AFE/AEC。
 * 工厂不加载模型、不开麦；open/close/reset 均通过 agent 的生命周期执行。 */
typedef struct {
    void *state;
} pet_ai_sr_t;

pet_ai_runtime_frontend_t pet_ai_sr_frontend(pet_ai_sr_t *frontend);
/* 独立验证 flash 模型包，不加载推理实例、不采集音频。 */
bool pet_ai_sr_verify_models(void);
