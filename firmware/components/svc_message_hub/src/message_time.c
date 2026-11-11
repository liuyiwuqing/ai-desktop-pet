#include "message_time.h"

#include <stddef.h>
#include <stdio.h>

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool parse_number(const char *text, size_t digits, unsigned *out)
{
    unsigned value = 0;
    for (size_t i = 0; i < digits; i++) {
        if (!is_digit(text[i])) {
            return false;
        }
        value = value * 10U + (unsigned)(text[i] - '0');
    }
    *out = value;
    return true;
}

static bool is_leap_year(unsigned year)
{
    return (year % 4U == 0U && year % 100U != 0U) || year % 400U == 0U;
}

static unsigned days_in_month(unsigned year, unsigned month)
{
    static const uint8_t days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month - 1];
}

/* Howard Hinnant 的 days_from_civil；返回相对 1970-01-01 的天数。 */
static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2U;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned adjusted_month = month > 2U ? month - 3U : month + 9U;
    const unsigned doy = (153U * adjusted_month + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

/* Howard Hinnant 的 civil_from_days；days 为相对 1970-01-01 的天数。 */
static void civil_from_days(int64_t days, int *year, unsigned *month, unsigned *day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460U + doe / 36524U - doe / 146096U) / 365U;
    const int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    *day = doy - (153U * mp + 2U) / 5U + 1U;
    *month = mp < 10U ? mp + 3U : mp - 9U;
    *year = y + (*month <= 2U ? 1 : 0);
}

bool pet_message_parse_iso8601(const char *text, uint32_t *epoch_s)
{
    if (text == NULL || epoch_s == NULL) {
        return false;
    }

    unsigned year;
    unsigned month;
    unsigned day;
    unsigned hour;
    unsigned minute;
    unsigned second;
    if (!parse_number(text, 4, &year) || text[4] != '-' || !parse_number(text + 5, 2, &month) ||
        text[7] != '-' || !parse_number(text + 8, 2, &day) || text[10] != 'T' ||
        !parse_number(text + 11, 2, &hour) || text[13] != ':' ||
        !parse_number(text + 14, 2, &minute) || text[16] != ':' ||
        !parse_number(text + 17, 2, &second)) {
        return false;
    }

    size_t pos = 19;
    if (text[pos] == '.') {
        pos++;
        size_t fraction_start = pos;
        while (is_digit(text[pos])) {
            pos++;
        }
        if (pos == fraction_start) {
            return false;
        }
    }

    int32_t offset_seconds = 0;
    if (text[pos] == 'Z') {
        pos++;
    } else if (text[pos] == '+' || text[pos] == '-') {
        const int32_t sign = text[pos] == '+' ? 1 : -1;
        unsigned offset_hour;
        unsigned offset_minute;
        if (!parse_number(text + pos + 1, 2, &offset_hour) || text[pos + 3] != ':' ||
            !parse_number(text + pos + 4, 2, &offset_minute)) {
            return false;
        }
        if (offset_hour > 14U || offset_minute > 59U) {
            return false;
        }
        offset_seconds = sign * (int32_t)(offset_hour * 3600U + offset_minute * 60U);
        pos += 6;
    } else {
        return false;
    }

    if (text[pos] != '\0') {
        return false;
    }

    if (year < 1970U || year > 2105U || month < 1U || month > 12U || day < 1U ||
        day > days_in_month(year, month) || hour > 23U || minute > 59U || second > 59U) {
        return false;
    }

    const int64_t days = days_from_civil((int)year, month, day);
    const int64_t seconds = days * 86400 + (int64_t)hour * 3600 + (int64_t)minute * 60 +
                            (int64_t)second - offset_seconds;
    if (seconds < 0 || seconds > (int64_t)UINT32_MAX) {
        return false;
    }
    *epoch_s = (uint32_t)seconds;
    return true;
}

bool pet_message_format_iso8601(uint32_t epoch_s, char *out, size_t capacity)
{
    if (out == NULL || capacity < 21) {
        return false;
    }

    const int64_t days = (int64_t)(epoch_s / 86400U);
    uint32_t remainder = epoch_s % 86400U;
    int year;
    unsigned month;
    unsigned day;
    civil_from_days(days, &year, &month, &day);
    if (year < 1970 || year > 9999) {
        return false;
    }

    const unsigned hour = (unsigned)(remainder / 3600U);
    remainder %= 3600U;
    const unsigned minute = (unsigned)(remainder / 60U);
    const unsigned second = (unsigned)(remainder % 60U);

    const int written = snprintf(out, capacity, "%04d-%02u-%02uT%02u:%02u:%02uZ", year,
                                 month, day, hour, minute, second);
    return written == 20;
}
