#include <assert.h>
#include <stdio.h>
#include "pet/hal/navigation.h"

int main(void)
{
    pet_navigation_filter_t filter = {0};
    pet_navigation_sample_t sample = {.x = 4095, .y = 2048, .valid = true};
    assert(pet_navigation_update(&filter, &sample, false, 0) == PET_NAV_NONE);
    assert(pet_navigation_update(&filter, &sample, false, 1000) == PET_NAV_NONE);
    sample.x = 2048;
    pet_navigation_update(&filter, &sample, false, 1010);
    pet_navigation_update(&filter, &sample, false, 1110);
    sample.y = 4095;
    assert(pet_navigation_update(&filter, &sample, false, 1120) == PET_NAV_NONE);
    assert(pet_navigation_update(&filter, &sample, false, 1150) == PET_NAV_DOWN);
    assert(pet_navigation_update(&filter, &sample, false, 1549) == PET_NAV_NONE);
    assert(pet_navigation_update(&filter, &sample, false, 1550) == PET_NAV_DOWN);
    assert(pet_navigation_update(&filter, &sample, false, 1710) == PET_NAV_DOWN);
    sample.valid = false;
    assert(pet_navigation_update(&filter, &sample, false, 1800) == PET_NAV_NONE);
    sample.valid = true;
    assert(pet_navigation_update(&filter, &sample, false, 2800) == PET_NAV_NONE);
    sample = (pet_navigation_sample_t){.x = 2048, .y = 2048, .valid = true, .pressed = true};
    pet_navigation_update(&filter, &sample, false, 3000);
    assert(pet_navigation_update(&filter, &sample, false, 4000) == PET_NAV_NONE);
    sample.pressed = false;
    pet_navigation_update(&filter, &sample, false, 4100);
    pet_navigation_update(&filter, &sample, false, 4200);
    sample.x = 2600; /* center noise below the enter threshold */
    pet_navigation_update(&filter, &sample, false, 4300);
    assert(pet_navigation_update(&filter, &sample, false, 4400) == PET_NAV_NONE);
    sample.x = 4095;
    pet_navigation_update(&filter, &sample, false, 4500);
    assert(pet_navigation_update(&filter, &sample, false, 4530) == PET_NAV_RIGHT);
    assert(pet_navigation_update(&filter, &sample, false, 5530) == PET_NAV_NONE);
    assert(pet_navigation_update(&filter, &sample, true, 5600) == PET_NAV_NONE);
    assert(pet_navigation_update(&filter, &sample, false, 6600) == PET_NAV_NONE);
    sample.x = 5000;
    assert(pet_navigation_update(&filter, &sample, false, 6700) == PET_NAV_NONE);
    puts("navigation: neutral startup, debounce and bounded repeat passed");
}
