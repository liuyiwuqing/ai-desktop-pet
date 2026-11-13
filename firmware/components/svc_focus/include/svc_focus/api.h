#pragma once
#include "esp_err.h"
#include "svc_focus/engine.h"
typedef uint64_t (*pet_focus_clock_t)(void);
esp_err_t pet_focus_init(pet_focus_clock_t clock);
esp_err_t pet_focus_start(uint64_t event_id);
esp_err_t pet_focus_pause(void);
esp_err_t pet_focus_resume(void);
esp_err_t pet_focus_stop(void);
esp_err_t pet_focus_snapshot(pet_focus_t *out);
/* 业务工作任务调用，返回待结算的 event_id；结算失败下次仍返回同一个 ID。 */
uint64_t pet_focus_tick(void);
void pet_focus_reward_settled(uint64_t event_id);

/* epoch 来自已对时的组合根/UI 时钟；服务分配唯一事件 ID，避免输入来源之间冲突。 */
esp_err_t pet_focus_begin(uint32_t epoch_s);
