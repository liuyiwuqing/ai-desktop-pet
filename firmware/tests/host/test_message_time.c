#include <stdint.h>
#include <stdio.h>

#include "message_time.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

static void check_parse(const char *text, uint32_t expected)
{
    uint32_t epoch = 0;
    CHECK(pet_message_parse_iso8601(text, &epoch));
    CHECK(epoch == expected);
}

static void check_invalid(const char *text)
{
    uint32_t epoch = 12345;
    CHECK(!pet_message_parse_iso8601(text, &epoch));
    CHECK(epoch == 12345);
}

int main(void)
{
    uint32_t epoch = 0;
    CHECK(!pet_message_parse_iso8601(NULL, &epoch));
    CHECK(!pet_message_parse_iso8601("1970-01-01T00:00:00Z", NULL));

    check_parse("1970-01-01T00:00:00Z", 0);
    check_parse("2024-02-29T00:00:00Z", 1709164800);
    check_parse("2026-11-10T02:00:00Z", 1794276000);
    check_parse("2026-11-10T10:00:00+08:00", 1794276000);
    check_parse("2026-11-10T10:00:00.123Z", 1794304800);
    check_parse("2026-11-11T10:00:00+08:00", 1794362400);

    check_invalid("");
    check_invalid("2026-13-01T00:00:00Z");
    check_invalid("2026-02-30T00:00:00Z");
    check_invalid("2025-02-29T00:00:00Z");
    check_invalid("2024-02-29T24:00:00Z");
    check_invalid("2024-02-29T00:60:00Z");
    check_invalid("2024-02-29T00:00:60Z");
    check_invalid("2024-02-29T00:00:00");
    check_invalid("2024-02-29T00:00:00+15:00");
    check_invalid("2024-02-29T00:00:00+08:60");
    check_invalid("2024-02-29T00:00:00.Z");
    check_invalid("2024-02-29T00:00:00Z ");
    check_invalid("2200-01-01T00:00:00Z");
    check_invalid("1969-12-31T23:59:59Z");

    if (s_failures == 0) {
        printf("host tests passed: message time\n");
    }
    return s_failures == 0 ? 0 : 1;
}
