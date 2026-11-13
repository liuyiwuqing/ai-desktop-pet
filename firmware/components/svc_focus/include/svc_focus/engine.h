#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { PET_FOCUS_IDLE,PET_FOCUS_RUNNING,PET_FOCUS_PAUSED,PET_FOCUS_COMPLETED,PET_FOCUS_CANCELLED } pet_focus_state_t;
typedef struct {
    uint64_t event_id,deadline_ms,last_ms,remaining_ms;
    pet_focus_state_t state;
    bool reward_pending;
} pet_focus_t;
/* 单一调用者或由外层互斥保护；不使用墙钟累计时长，重启不恢复未完成计时。 */
bool pet_focus_begin_at(pet_focus_t *focus,uint64_t event_id,uint64_t now_ms);
bool pet_focus_pause_at(pet_focus_t *focus,uint64_t now_ms);
bool pet_focus_resume_at(pet_focus_t *focus,uint64_t now_ms);
void pet_focus_cancel(pet_focus_t *focus);
bool pet_focus_step(pet_focus_t *focus,uint64_t now_ms);
void pet_focus_settled(pet_focus_t *focus);
