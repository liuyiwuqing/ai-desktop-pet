#include "ui_owner.h"
#include <stdatomic.h>
static atomic_flag owner=ATOMIC_FLAG_INIT;
bool pet_ui_owner_claim(void)
{return !atomic_flag_test_and_set_explicit(&owner,memory_order_acquire);}
void pet_ui_owner_release(void)
{atomic_flag_clear_explicit(&owner,memory_order_release);}
