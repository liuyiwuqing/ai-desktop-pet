#include <stdint.h>
#include <stdio.h>

#include "pet/event.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

int main(void)
{
    const pet_event_t valid = {
        .id = PET_EVT_TICK,
        .payload_len = PET_EVENT_PAYLOAD_MAX,
    };
    CHECK(pet_event_is_valid(&valid));
    CHECK(!pet_event_is_valid(NULL));

    pet_event_t bad_id = {.id = PET_EVT_COUNT};
    CHECK(!pet_event_is_valid(&bad_id));

    pet_event_t oversize = {
        .id = PET_EVT_BOOT,
        .payload_len = PET_EVENT_PAYLOAD_MAX + 1,
    };
    CHECK(!pet_event_is_valid(&oversize));

    CHECK(sizeof(pet_event_t) <= 48);

    if (s_failures == 0) {
        printf("host tests passed: event contract\n");
    }
    return s_failures == 0 ? 0 : 1;
}
