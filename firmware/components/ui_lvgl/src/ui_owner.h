#pragma once
#include <stdbool.h>
/* Both startup paths own the same global LVGL state. Claim before allocating or creating a task. */
bool pet_ui_owner_claim(void);
void pet_ui_owner_release(void);
