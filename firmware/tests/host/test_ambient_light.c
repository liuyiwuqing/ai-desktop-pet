#include <assert.h>
#include <stdio.h>
#include "pet/hal/ambient_light.h"

int main(void)
{
    pet_ambient_filter_t filter = {0};
    assert(pet_ambient_brightness(&filter, 0, true, 0, 80) == 15);
    assert(pet_ambient_brightness(&filter, 4095, true, 100, 80) == 15);
    assert(pet_ambient_brightness(&filter, 4095, true, 500, 80) == 20);
    assert(pet_ambient_brightness(&filter, 4095, true, 600, 10) == 10);
    assert(pet_ambient_brightness(&filter, 4095, true, 700, 0) == 0);
    assert(pet_ambient_brightness(&filter, 4095, false, 800, 60) == 60);
    assert(pet_ambient_brightness(&filter, 4095, true, 900, 60) == 60);
    for (unsigned now = 1400; now <= 12000; now += 500) {
        const uint8_t brightness = pet_ambient_brightness(&filter, 0, true, now, 60);
        assert(brightness >= 15 && brightness <= 60);
    }
    assert(pet_ambient_brightness(&filter, 0, true, 12500, 60) == 15);
    assert(pet_ambient_brightness(&filter, 5000, true, 13000, 30) == 30);
    assert(pet_ambient_brightness(&filter, 4095, false, 13500, 255) == 100);
    filter = (pet_ambient_filter_t){0};
    assert(pet_ambient_brightness(&filter, 0, true, UINT32_MAX - 100, 80) == 15);
    assert(pet_ambient_brightness(&filter, 4095, true, 399, 80) == 20);
    puts("ambient light: dimming, smoothing, manual ceiling, off and error fallback passed");
}
