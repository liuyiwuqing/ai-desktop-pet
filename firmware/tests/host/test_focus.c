#include <assert.h>
#include <stdio.h>
#include "svc_focus/engine.h"
int main(void)
{
    pet_focus_t f={0};
    assert(pet_focus_begin_at(&f,1,100));
    assert(!pet_focus_step(&f,500099));
    assert(pet_focus_pause_at(&f,500100));
    assert(!pet_focus_step(&f,2000100));
    assert(pet_focus_resume_at(&f,2000100));
    assert(!pet_focus_step(&f,3000099));
    assert(pet_focus_step(&f,3000100));
    assert(!pet_focus_step(&f,3000101));
    assert(!pet_focus_begin_at(&f,2,3000102)); /* 完成结果尚未结算。 */
    pet_focus_settled(&f);
    assert(pet_focus_begin_at(&f,2,3000200));
    pet_focus_cancel(&f);
    assert(!pet_focus_step(&f,9000000));
    assert(pet_focus_begin_at(&f,3,9000100));
    assert(!pet_focus_step(&f,9000000)&&f.state==PET_FOCUS_CANCELLED);
    puts("Focus: monotonic 25min, pause/resume, cancel and exactly-once completion passed");
}
