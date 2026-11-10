#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    pet_event_t valid;
    pet_event_prepare(&valid, PET_EVT_TICK, PET_EVENT_SOURCE_CORE, PET_EVENT_PRIORITY_NORMAL);
    valid.payload_len = PET_EVENT_PAYLOAD_MAX;
    CHECK(pet_event_is_valid(&valid));
    CHECK(valid.schema_version == PET_EVENT_SCHEMA_VERSION);
    CHECK(valid.sequence == 0); /* 由总线发布时赋值 */
    CHECK(!pet_event_is_valid(NULL));

    pet_event_t bad_id;
    pet_event_prepare(&bad_id, PET_EVT_TICK, PET_EVENT_SOURCE_CORE, PET_EVENT_PRIORITY_LOW);
    bad_id.id = PET_EVT_COUNT;
    CHECK(!pet_event_is_valid(&bad_id));

    pet_event_t oversize;
    pet_event_prepare(&oversize, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_LOW);
    oversize.payload_len = PET_EVENT_PAYLOAD_MAX + 1;
    CHECK(!pet_event_is_valid(&oversize));

    pet_event_t bad_version;
    pet_event_prepare(&bad_version, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_LOW);
    bad_version.schema_version = PET_EVENT_SCHEMA_VERSION + 1;
    CHECK(!pet_event_is_valid(&bad_version));

    pet_event_t bad_priority;
    pet_event_prepare(&bad_priority, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_LOW);
    bad_priority.priority = PET_EVENT_PRIORITY_COUNT;
    CHECK(!pet_event_is_valid(&bad_priority));

    pet_event_t bad_source;
    pet_event_prepare(&bad_source, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_LOW);
    bad_source.source = PET_EVENT_SOURCE_COUNT;
    CHECK(!pet_event_is_valid(&bad_source));

    pet_event_t prepared;
    memset(&prepared, 0xFF, sizeof(prepared));
    pet_event_prepare(&prepared, PET_EVT_BOOT, PET_EVENT_SOURCE_APP, PET_EVENT_PRIORITY_HIGH);
    CHECK(prepared.payload_len == 0);
    for (size_t i = 0; i < sizeof(prepared.payload); i++) {
        CHECK(prepared.payload[i] == 0);
    }

    CHECK(sizeof(pet_event_t) <= 96);
    CHECK(PET_EVENT_PAYLOAD_MAX == 64);

    if (s_failures == 0) {
        printf("host tests passed: event contract\n");
    }
    return s_failures == 0 ? 0 : 1;
}
