#include "svc_focus/engine.h"
#define DURATION_MS 1500000u
bool pet_focus_begin_at(pet_focus_t *f,uint64_t id,uint64_t now)
{
    if (!f||!id||id<=f->event_id||f->reward_pending||f->state==PET_FOCUS_RUNNING||f->state==PET_FOCUS_PAUSED||now>UINT64_MAX-DURATION_MS) return false;
    *f=(pet_focus_t){.event_id=id,.deadline_ms=now+DURATION_MS,.last_ms=now,.remaining_ms=DURATION_MS,.state=PET_FOCUS_RUNNING};
    return true;
}
bool pet_focus_step(pet_focus_t *f,uint64_t now)
{
    if (!f) return false;
    if (now<f->last_ms) {pet_focus_cancel(f);return false;}
    f->last_ms=now;
    if (f->state!=PET_FOCUS_RUNNING) return false;
    f->remaining_ms=now>=f->deadline_ms?0:f->deadline_ms-now;
    if (!f->remaining_ms) { f->state=PET_FOCUS_COMPLETED;f->reward_pending=true;return true; }
    return false;
}
bool pet_focus_pause_at(pet_focus_t *f,uint64_t now)
{
    if (!f||f->state!=PET_FOCUS_RUNNING) return false;
    if (pet_focus_step(f,now)||f->state!=PET_FOCUS_RUNNING) return false;
    f->state=PET_FOCUS_PAUSED;return true;
}
bool pet_focus_resume_at(pet_focus_t *f,uint64_t now)
{
    if (!f||f->state!=PET_FOCUS_PAUSED||now<f->last_ms||now>UINT64_MAX-f->remaining_ms) return false;
    f->deadline_ms=now+f->remaining_ms;f->last_ms=now;f->state=PET_FOCUS_RUNNING;return true;
}
void pet_focus_cancel(pet_focus_t *f)
{ if (f&&(f->state==PET_FOCUS_RUNNING||f->state==PET_FOCUS_PAUSED)) { f->state=PET_FOCUS_CANCELLED;f->remaining_ms=0; } }
void pet_focus_settled(pet_focus_t *f) { if (f) f->reward_pending=false; }
