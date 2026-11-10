#include <assert.h>
#include <stdio.h>

#include "pet/hal/climate_decode.h"

static void test_valid_frame(void)
{
    const uint8_t frame[5] = {50, 0, 24, 0, 74};
    float temperature = 0.0f;
    float humidity = 0.0f;

    assert(pet_climate_decode_dht11(frame, &temperature, &humidity) == PET_CLIMATE_ERR_NONE);
    assert(humidity > 49.99f && humidity < 50.01f);
    assert(temperature > 23.99f && temperature < 24.01f);

    const uint8_t with_decimal[5] = {58, 5, 24, 6, 93};
    assert(pet_climate_decode_dht11(with_decimal, &temperature, &humidity) ==
           PET_CLIMATE_ERR_NONE);
    assert(humidity > 58.49f && humidity < 58.51f);
    assert(temperature > 24.59f && temperature < 24.61f);
}

static void test_checksum(void)
{
    const uint8_t frame[5] = {50, 0, 24, 0, 0};
    float temperature = 0.0f;
    float humidity = 0.0f;

    assert(pet_climate_decode_dht11(frame, &temperature, &humidity) ==
           PET_CLIMATE_ERR_CHECKSUM);
}

static void test_range(void)
{
    float temperature = 0.0f;
    float humidity = 0.0f;

    const uint8_t hot[5] = {50, 0, 70, 0, 120};
    assert(pet_climate_decode_dht11(hot, &temperature, &humidity) == PET_CLIMATE_ERR_RANGE);

    const uint8_t wet[5] = {100, 1, 24, 0, 125};
    assert(pet_climate_decode_dht11(wet, &temperature, &humidity) == PET_CLIMATE_ERR_RANGE);
}

int main(void)
{
    test_valid_frame();
    test_checksum();
    test_range();
    printf("test_climate_decode: ok\n");
    return 0;
}
