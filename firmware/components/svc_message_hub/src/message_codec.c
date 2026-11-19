#include "message_codec.h"

#include <stdio.h>
#include <string.h>

#include "message_time.h"

/* 有界 JSON 读取器：只覆盖消息信封需要的子集，拒绝未知字段与重复字段。 */

typedef struct {
    const char *cur;
    const char *end;
} cursor_t;

typedef enum {
    STRING_OK = 0,
    STRING_TOO_LONG,
    STRING_INVALID,
} string_result_t;

typedef struct {
    unsigned seen;
} field_mask_t;

enum {
    FIELD_SCHEMA_VERSION = 1U << 0,
    FIELD_ID = 1U << 1,
    FIELD_SOURCE = 1U << 2,
    FIELD_TYPE = 1U << 3,
    FIELD_PRIORITY = 1U << 4,
    FIELD_TITLE = 1U << 5,
    FIELD_BODY = 1U << 6,
    FIELD_PAYLOAD = 1U << 7,
    FIELD_CREATED_AT = 1U << 8,
    FIELD_EXPIRES_AT = 1U << 9,
    FIELD_DISPLAY = 1U << 10,
    FIELD_REQUIRE_ACK = 1U << 11,
    FIELD_DEDUPE_KEY = 1U << 12,
};

#define FIELD_REQUIRED_MASK                                                       \
    (FIELD_SCHEMA_VERSION | FIELD_ID | FIELD_SOURCE | FIELD_TYPE | FIELD_PRIORITY | \
     FIELD_TITLE | FIELD_BODY | FIELD_PAYLOAD | FIELD_CREATED_AT | FIELD_EXPIRES_AT | \
     FIELD_DISPLAY | FIELD_REQUIRE_ACK)

enum {
    DISPLAY_TEMPLATE = 1U << 0,
    DISPLAY_DURATION = 1U << 1,
    DISPLAY_SOUND = 1U << 2,
    DISPLAY_ICON = 1U << 3,
};

#define DISPLAY_REQUIRED_MASK (DISPLAY_TEMPLATE | DISPLAY_DURATION | DISPLAY_SOUND)

static void skip_ws(cursor_t *cursor)
{
    while (cursor->cur < cursor->end) {
        const char c = *cursor->cur;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        cursor->cur++;
    }
}

static bool consume(cursor_t *cursor, char expected)
{
    skip_ws(cursor);
    if (cursor->cur >= cursor->end || *cursor->cur != expected) {
        return false;
    }
    cursor->cur++;
    return true;
}

static bool peek(cursor_t *cursor, char *out)
{
    skip_ws(cursor);
    if (cursor->cur >= cursor->end) {
        return false;
    }
    *out = *cursor->cur;
    return true;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static bool read_hex4(cursor_t *cursor, uint32_t *out)
{
    if ((size_t)(cursor->end - cursor->cur) < 4) {
        return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) {
        const int digit = hex_value(cursor->cur[i]);
        if (digit < 0) {
            return false;
        }
        value = (value << 4) | (uint32_t)digit;
    }
    cursor->cur += 4;
    *out = value;
    return true;
}

static bool append_byte(char *out, size_t capacity, size_t *written, bool decode, char byte)
{
    if (!decode) {
        return true;
    }
    if (*written + 1 >= capacity) {
        return false;
    }
    out[*written] = byte;
    (*written)++;
    return true;
}

static bool append_codepoint(char *out, size_t capacity, size_t *written, bool decode,
                             uint32_t cp)
{
    if (cp <= 0x7F) {
        return append_byte(out, capacity, written, decode, (char)cp);
    }
    if (cp <= 0x7FF) {
        return append_byte(out, capacity, written, decode, (char)(0xC0 | (cp >> 6))) &&
               append_byte(out, capacity, written, decode, (char)(0x80 | (cp & 0x3F)));
    }
    if (cp <= 0xFFFF) {
        return append_byte(out, capacity, written, decode, (char)(0xE0 | (cp >> 12))) &&
               append_byte(out, capacity, written, decode, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
               append_byte(out, capacity, written, decode, (char)(0x80 | (cp & 0x3F)));
    }
    return append_byte(out, capacity, written, decode, (char)(0xF0 | (cp >> 18))) &&
           append_byte(out, capacity, written, decode, (char)(0x80 | ((cp >> 12) & 0x3F))) &&
           append_byte(out, capacity, written, decode, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
           append_byte(out, capacity, written, decode, (char)(0x80 | (cp & 0x3F)));
}

static string_result_t parse_string(cursor_t *cursor, char *out, size_t capacity)
{
    const bool decode = out != NULL && capacity > 0;
    size_t written = 0;
    bool too_long = false;

    if (decode) {
        out[0] = '\0';
    }
    if (!consume(cursor, '"')) {
        return STRING_INVALID;
    }

    while (cursor->cur < cursor->end) {
        const unsigned char c = (unsigned char)*cursor->cur++;
        if (c == '"') {
            if (decode) {
                out[written] = '\0';
            }
            return too_long ? STRING_TOO_LONG : STRING_OK;
        }
        if (c < 0x20) {
            return STRING_INVALID;
        }

        char bytes[4];
        size_t byte_count = 1;
        bytes[0] = (char)c;

        if (c == '\\') {
            if (cursor->cur >= cursor->end) {
                return STRING_INVALID;
            }
            const char escape = *cursor->cur++;
            switch (escape) {
            case '"':
            case '\\':
            case '/':
                bytes[0] = escape;
                break;
            case 'b':
                bytes[0] = '\b';
                break;
            case 'f':
                bytes[0] = '\f';
                break;
            case 'n':
                bytes[0] = '\n';
                break;
            case 'r':
                bytes[0] = '\r';
                break;
            case 't':
                bytes[0] = '\t';
                break;
            case 'u': {
                uint32_t cp = 0;
                if (!read_hex4(cursor, &cp)) {
                    return STRING_INVALID;
                }
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if ((size_t)(cursor->end - cursor->cur) < 2 || cursor->cur[0] != '\\' ||
                        cursor->cur[1] != 'u') {
                        return STRING_INVALID;
                    }
                    cursor->cur += 2;
                    uint32_t low = 0;
                    if (!read_hex4(cursor, &low) || low < 0xDC00 || low > 0xDFFF) {
                        return STRING_INVALID;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return STRING_INVALID;
                }
                if (cp == 0) {
                    return STRING_INVALID;
                }
                if (!append_codepoint(out, capacity, &written, decode, cp)) {
                    too_long = true;
                }
                continue;
            }
            default:
                return STRING_INVALID;
            }
        }

        for (size_t i = 0; i < byte_count; i++) {
            if (!append_byte(out, capacity, &written, decode, bytes[i])) {
                too_long = true;
                break;
            }
        }
    }
    return STRING_INVALID;
}

static bool skip_number(cursor_t *cursor)
{
    skip_ws(cursor);
    const char *start = cursor->cur;
    if (cursor->cur < cursor->end && *cursor->cur == '-') {
        cursor->cur++;
    }
    if (cursor->cur >= cursor->end) {
        return false;
    }
    if (*cursor->cur == '0') {
        cursor->cur++;
    } else if (*cursor->cur >= '1' && *cursor->cur <= '9') {
        while (cursor->cur < cursor->end && *cursor->cur >= '0' && *cursor->cur <= '9') {
            cursor->cur++;
        }
    } else {
        return false;
    }
    if (cursor->cur < cursor->end && *cursor->cur == '.') {
        cursor->cur++;
        if (cursor->cur >= cursor->end || *cursor->cur < '0' || *cursor->cur > '9') {
            return false;
        }
        while (cursor->cur < cursor->end && *cursor->cur >= '0' && *cursor->cur <= '9') {
            cursor->cur++;
        }
    }
    if (cursor->cur < cursor->end && (*cursor->cur == 'e' || *cursor->cur == 'E')) {
        cursor->cur++;
        if (cursor->cur < cursor->end && (*cursor->cur == '+' || *cursor->cur == '-')) {
            cursor->cur++;
        }
        if (cursor->cur >= cursor->end || *cursor->cur < '0' || *cursor->cur > '9') {
            return false;
        }
        while (cursor->cur < cursor->end && *cursor->cur >= '0' && *cursor->cur <= '9') {
            cursor->cur++;
        }
    }
    return cursor->cur > start;
}

static bool skip_value(cursor_t *cursor, uint8_t depth);

static bool skip_object(cursor_t *cursor, uint8_t depth)
{
    if (!consume(cursor, '{')) {
        return false;
    }
    char next = '\0';
    if (!peek(cursor, &next)) {
        return false;
    }
    if (next == '}') {
        cursor->cur++;
        return true;
    }
    while (true) {
        if (parse_string(cursor, NULL, 0) != STRING_OK) {
            return false;
        }
        if (!consume(cursor, ':') || !skip_value(cursor, depth)) {
            return false;
        }
        if (!peek(cursor, &next)) {
            return false;
        }
        if (next == '}') {
            cursor->cur++;
            return true;
        }
        if (next != ',' || !consume(cursor, ',')) {
            return false;
        }
    }
}

static bool skip_array(cursor_t *cursor, uint8_t depth)
{
    if (!consume(cursor, '[')) {
        return false;
    }
    char next = '\0';
    if (!peek(cursor, &next)) {
        return false;
    }
    if (next == ']') {
        cursor->cur++;
        return true;
    }
    while (true) {
        if (!skip_value(cursor, depth)) {
            return false;
        }
        if (!peek(cursor, &next)) {
            return false;
        }
        if (next == ']') {
            cursor->cur++;
            return true;
        }
        if (next != ',' || !consume(cursor, ',')) {
            return false;
        }
    }
}

static bool skip_value(cursor_t *cursor, uint8_t depth)
{
    if (depth > 16) {
        return false;
    }
    char next = '\0';
    if (!peek(cursor, &next)) {
        return false;
    }
    if (next == '{') {
        return skip_object(cursor, (uint8_t)(depth + 1));
    }
    if (next == '[') {
        return skip_array(cursor, (uint8_t)(depth + 1));
    }
    if (next == '"') {
        return parse_string(cursor, NULL, 0) == STRING_OK;
    }
    if (next == 't') {
        if ((size_t)(cursor->end - cursor->cur) < 4 || strncmp(cursor->cur, "true", 4) != 0) {
            return false;
        }
        cursor->cur += 4;
        return true;
    }
    if (next == 'f') {
        if ((size_t)(cursor->end - cursor->cur) < 5 || strncmp(cursor->cur, "false", 5) != 0) {
            return false;
        }
        cursor->cur += 5;
        return true;
    }
    if (next == 'n') {
        if ((size_t)(cursor->end - cursor->cur) < 4 || strncmp(cursor->cur, "null", 4) != 0) {
            return false;
        }
        cursor->cur += 4;
        return true;
    }
    return skip_number(cursor);
}

static bool parse_uint(cursor_t *cursor, uint32_t *out)
{
    skip_ws(cursor);
    if (cursor->cur >= cursor->end || *cursor->cur < '0' || *cursor->cur > '9') {
        return false;
    }
    uint64_t value = 0;
    if (*cursor->cur == '0') {
        cursor->cur++;
        if (cursor->cur < cursor->end && *cursor->cur >= '0' && *cursor->cur <= '9') {
            return false;
        }
    } else {
        while (cursor->cur < cursor->end && *cursor->cur >= '0' && *cursor->cur <= '9') {
            value = value * 10 + (uint32_t)(*cursor->cur - '0');
            if (value > UINT32_MAX) {
                return false;
            }
            cursor->cur++;
        }
    }
    *out = (uint32_t)value;
    return true;
}

static bool parse_bool(cursor_t *cursor, bool *out)
{
    skip_ws(cursor);
    if ((size_t)(cursor->end - cursor->cur) >= 4 &&
        strncmp(cursor->cur, "true", 4) == 0) {
        cursor->cur += 4;
        *out = true;
        return true;
    }
    if ((size_t)(cursor->end - cursor->cur) >= 5 &&
        strncmp(cursor->cur, "false", 5) == 0) {
        cursor->cur += 5;
        *out = false;
        return true;
    }
    return false;
}

static bool mark_field(field_mask_t *mask, unsigned field)
{
    if ((mask->seen & field) != 0) {
        return false;
    }
    mask->seen |= field;
    return true;
}

static pet_message_error_t parse_priority(const char *text, pet_message_priority_t *out)
{
    if (strcmp(text, "critical") == 0) {
        *out = PET_MESSAGE_PRIORITY_CRITICAL;
    } else if (strcmp(text, "high") == 0) {
        *out = PET_MESSAGE_PRIORITY_HIGH;
    } else if (strcmp(text, "normal") == 0) {
        *out = PET_MESSAGE_PRIORITY_NORMAL;
    } else if (strcmp(text, "low") == 0) {
        *out = PET_MESSAGE_PRIORITY_LOW;
    } else {
        return PET_MESSAGE_ERR_PRIORITY;
    }
    return PET_MESSAGE_OK;
}

static pet_message_error_t parse_template(const char *text, pet_message_template_t *out)
{
    if (strcmp(text, "text_card") == 0) {
        *out = PET_MESSAGE_TEMPLATE_TEXT_CARD;
    } else if (strcmp(text, "comment_card") == 0) {
        *out = PET_MESSAGE_TEMPLATE_COMMENT_CARD;
    } else if (strcmp(text, "alert_card") == 0) {
        *out = PET_MESSAGE_TEMPLATE_ALERT_CARD;
    } else if (strcmp(text, "growth_card") == 0) {
        *out = PET_MESSAGE_TEMPLATE_GROWTH_CARD;
    } else if (strcmp(text, "system_card") == 0) {
        *out = PET_MESSAGE_TEMPLATE_SYSTEM_CARD;
    } else {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    return PET_MESSAGE_OK;
}

static pet_message_error_t parse_sound(const char *text, pet_message_sound_t *out)
{
    if (strcmp(text, "chime") == 0) {
        *out = PET_MESSAGE_SOUND_CHIME;
    } else if (strcmp(text, "alert") == 0) {
        *out = PET_MESSAGE_SOUND_ALERT;
    } else if (strcmp(text, "none") == 0) {
        *out = PET_MESSAGE_SOUND_NONE;
    } else {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    return PET_MESSAGE_OK;
}

static bool type_valid(const char *text)
{
    return strcmp(text, "comment") == 0 || strcmp(text, "alert") == 0 ||
           strcmp(text, "growth") == 0 || strcmp(text, "system") == 0 ||
           strcmp(text, "text") == 0;
}

static pet_message_error_t parse_display(cursor_t *cursor, pet_message_view_t *out)
{
    if (!consume(cursor, '{')) {
        return PET_MESSAGE_ERR_DISPLAY;
    }

    unsigned seen = 0;
    char next = '\0';
    if (!peek(cursor, &next)) {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    if (next == '}') {
        cursor->cur++;
        return PET_MESSAGE_ERR_DISPLAY;
    }

    while (true) {
        char key[16];
        const string_result_t key_result = parse_string(cursor, key, sizeof(key));
        if (key_result != STRING_OK || !consume(cursor, ':')) {
            return PET_MESSAGE_ERR_DISPLAY;
        }

        if (strcmp(key, "template") == 0) {
            char text[16];
            if (parse_string(cursor, text, sizeof(text)) != STRING_OK || (seen & DISPLAY_TEMPLATE) != 0 ||
                parse_template(text, &out->template_id) != PET_MESSAGE_OK) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            seen |= DISPLAY_TEMPLATE;
        } else if (strcmp(key, "durationMs") == 0) {
            uint32_t duration = 0;
            if (!parse_uint(cursor, &duration) || duration < 5000 || duration > 30000 ||
                (seen & DISPLAY_DURATION) != 0) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            out->duration_ms = duration;
            seen |= DISPLAY_DURATION;
        } else if (strcmp(key, "sound") == 0) {
            char text[16];
            if (parse_string(cursor, text, sizeof(text)) != STRING_OK || (seen & DISPLAY_SOUND) != 0 ||
                parse_sound(text, &out->sound) != PET_MESSAGE_OK) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            seen |= DISPLAY_SOUND;
        } else if (strcmp(key, "icon") == 0) {
            char icon[33];
            if (parse_string(cursor, icon, sizeof(icon)) != STRING_OK ||
                (seen & DISPLAY_ICON) != 0) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            seen |= DISPLAY_ICON;
        } else {
            return PET_MESSAGE_ERR_DISPLAY;
        }

        if (!peek(cursor, &next)) {
            return PET_MESSAGE_ERR_DISPLAY;
        }
        if (next == '}') {
            cursor->cur++;
            break;
        }
        if (next != ',' || !consume(cursor, ',')) {
            return PET_MESSAGE_ERR_DISPLAY;
        }
    }

    return (seen & DISPLAY_REQUIRED_MASK) == DISPLAY_REQUIRED_MASK ? PET_MESSAGE_OK
                                                                   : PET_MESSAGE_ERR_DISPLAY;
}

static pet_message_error_t parse_time_field(cursor_t *cursor, uint32_t *out)
{
    char text[32];
    if (parse_string(cursor, text, sizeof(text)) != STRING_OK ||
        !pet_message_parse_iso8601(text, out) || *out == 0) {
        return PET_MESSAGE_ERR_TIME;
    }
    return PET_MESSAGE_OK;
}

pet_message_error_t pet_message_parse(const char *json, size_t length,
                                      pet_message_view_t *out)
{
    if (json == NULL || out == NULL || length == 0 || length > PET_MESSAGE_JSON_MAX) {
        return PET_MESSAGE_ERR_ARG;
    }

    memset(out, 0, sizeof(*out));
    cursor_t cursor = {.cur = json, .end = json + length};
    field_mask_t fields = {0};

    if (!consume(&cursor, '{')) {
        return PET_MESSAGE_ERR_SCHEMA;
    }

    char next = '\0';
    if (!peek(&cursor, &next)) {
        return PET_MESSAGE_ERR_SCHEMA;
    }
    if (next == '}') {
        return PET_MESSAGE_ERR_SCHEMA;
    }

    while (true) {
        char key[20];
        const string_result_t key_result = parse_string(&cursor, key, sizeof(key));
        if (key_result != STRING_OK || !consume(&cursor, ':')) {
            return PET_MESSAGE_ERR_SCHEMA;
        }

        if (strcmp(key, "schemaVersion") == 0) {
            uint32_t version = 0;
            if (!parse_uint(&cursor, &version) || version != PET_MESSAGE_SCHEMA_VERSION ||
                !mark_field(&fields, FIELD_SCHEMA_VERSION)) {
                return PET_MESSAGE_ERR_SCHEMA;
            }
            out->schema_version = (uint16_t)version;
        } else if (strcmp(key, "id") == 0) {
            if (parse_string(&cursor, out->id, sizeof(out->id)) != STRING_OK ||
                !mark_field(&fields, FIELD_ID)) {
                return PET_MESSAGE_ERR_ID;
            }
        } else if (strcmp(key, "source") == 0) {
            if (parse_string(&cursor, out->source, sizeof(out->source)) != STRING_OK ||
                !mark_field(&fields, FIELD_SOURCE)) {
                return PET_MESSAGE_ERR_SOURCE;
            }
        } else if (strcmp(key, "type") == 0) {
            char type[16];
            if (parse_string(&cursor, type, sizeof(type)) != STRING_OK || !type_valid(type) ||
                !mark_field(&fields, FIELD_TYPE)) {
                return PET_MESSAGE_ERR_SCHEMA;
            }
        } else if (strcmp(key, "priority") == 0) {
            char text[16];
            if (parse_string(&cursor, text, sizeof(text)) != STRING_OK ||
                parse_priority(text, &out->priority) != PET_MESSAGE_OK ||
                !mark_field(&fields, FIELD_PRIORITY)) {
                return PET_MESSAGE_ERR_PRIORITY;
            }
        } else if (strcmp(key, "title") == 0) {
            const string_result_t result = parse_string(&cursor, out->title, sizeof(out->title));
            if (result != STRING_OK || !mark_field(&fields, FIELD_TITLE)) {
                return PET_MESSAGE_ERR_TITLE;
            }
        } else if (strcmp(key, "body") == 0) {
            const string_result_t result = parse_string(&cursor, out->body, sizeof(out->body));
            if (result != STRING_OK || !mark_field(&fields, FIELD_BODY)) {
                return PET_MESSAGE_ERR_BODY;
            }
        } else if (strcmp(key, "payload") == 0) {
            if (!skip_value(&cursor, 0) || !mark_field(&fields, FIELD_PAYLOAD)) {
                return PET_MESSAGE_ERR_SCHEMA;
            }
        } else if (strcmp(key, "createdAt") == 0) {
            if (parse_time_field(&cursor, &out->created_at_epoch_s) != PET_MESSAGE_OK ||
                !mark_field(&fields, FIELD_CREATED_AT)) {
                return PET_MESSAGE_ERR_TIME;
            }
        } else if (strcmp(key, "expiresAt") == 0) {
            if (parse_time_field(&cursor, &out->expires_at_epoch_s) != PET_MESSAGE_OK ||
                !mark_field(&fields, FIELD_EXPIRES_AT)) {
                return PET_MESSAGE_ERR_TIME;
            }
        } else if (strcmp(key, "display") == 0) {
            if (parse_display(&cursor, out) != PET_MESSAGE_OK ||
                !mark_field(&fields, FIELD_DISPLAY)) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
        } else if (strcmp(key, "requireAck") == 0) {
            if (!parse_bool(&cursor, &out->require_ack) ||
                !mark_field(&fields, FIELD_REQUIRE_ACK)) {
                return PET_MESSAGE_ERR_SCHEMA;
            }
        } else if (strcmp(key, "dedupeKey") == 0) {
            if (parse_string(&cursor, out->dedupe_key, sizeof(out->dedupe_key)) != STRING_OK ||
                !mark_field(&fields, FIELD_DEDUPE_KEY)) {
                return PET_MESSAGE_ERR_SCHEMA;
            }
        } else {
            return PET_MESSAGE_ERR_SCHEMA;
        }

        if (!peek(&cursor, &next)) {
            return PET_MESSAGE_ERR_SCHEMA;
        }
        if (next == '}') {
            cursor.cur++;
            break;
        }
        if (next != ',' || !consume(&cursor, ',')) {
            return PET_MESSAGE_ERR_SCHEMA;
        }
    }

    skip_ws(&cursor);
    if (cursor.cur != cursor.end) {
        return PET_MESSAGE_ERR_SCHEMA;
    }
    if ((fields.seen & FIELD_REQUIRED_MASK) != FIELD_REQUIRED_MASK) {
        return PET_MESSAGE_ERR_SCHEMA;
    }

    out->state = PET_MESSAGE_STATE_RECEIVED;
    return PET_MESSAGE_OK;
}

static const char *ack_state_name(pet_message_ack_state_t state)
{
    switch (state) {
    case PET_MESSAGE_STATE_RECEIVED:
        return "received";
    case PET_MESSAGE_STATE_DISPLAYED:
        return "displayed";
    case PET_MESSAGE_STATE_READ:
        return "read";
    default:
        return NULL;
    }
}

int pet_message_ack_format(char *out, size_t capacity, const char *message_id,
                           pet_message_ack_state_t state, uint32_t at_epoch_s,
                           uint8_t queue_depth)
{
    if (out == NULL || capacity == 0 || message_id == NULL) {
        return -1;
    }
    const char *state_name = ack_state_name(state);
    if (state_name == NULL || queue_depth > 32) {
        return -1;
    }

    char timestamp[21];
    if (!pet_message_format_iso8601(at_epoch_s, timestamp, sizeof(timestamp))) {
        return -1;
    }

    const int written = snprintf(out, capacity,
                                 "{\"schemaVersion\":%d,\"messageId\":\"%s\","
                                 "\"state\":\"%s\",\"at\":\"%s\",\"queueDepth\":%u}",
                                 PET_MESSAGE_SCHEMA_VERSION, message_id, state_name,
                                 timestamp, (unsigned)queue_depth);
    if (written < 0 || (size_t)written >= capacity) {
        return -1;
    }
    return written;
}

/* DeviceText.fit 输出 UTF-8 且移除 ISO control；按字节限制，不把汉字数当字节数。 */
static bool nexus_text_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;
    while (*p != 0) {
        uint32_t cp = *p++;
        unsigned rest = 0;
        uint32_t minimum = 0;
        if (cp >= 0xC2 && cp <= 0xDF) {
            cp &= 0x1F;
            rest = 1;
            minimum = 0x80;
        } else if (cp >= 0xE0 && cp <= 0xEF) {
            cp &= 0x0F;
            rest = 2;
            minimum = 0x800;
        } else if (cp >= 0xF0 && cp <= 0xF4) {
            cp &= 0x07;
            rest = 3;
            minimum = 0x10000;
        } else if (cp >= 0x80) {
            return false;
        }
        for (unsigned i = 0; i < rest; ++i) {
            if ((*p & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (*p++ & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) ||
            cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
            return false;
    }
    return true;
}

static pet_message_error_t parse_nexus_display(cursor_t *cursor, pet_message_view_t *out)
{
    if (!consume(cursor, '{')) {
        return PET_MESSAGE_ERR_DISPLAY;
    }

    unsigned seen = 0;
    char next = '\0';
    if (!peek(cursor, &next)) {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    if (next == '}') {
        return PET_MESSAGE_ERR_DISPLAY;
    }

    for (;;) {
        char key[16];
        if (parse_string(cursor, key, sizeof(key)) != STRING_OK || !consume(cursor, ':')) {
            return PET_MESSAGE_ERR_DISPLAY;
        }
        if (strcmp(key, "durationMs") == 0) {
            uint32_t duration = 0;
            if (!parse_uint(cursor, &duration) || duration < 5000 || duration > 30000 ||
                (seen & 1u) != 0) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            out->duration_ms = duration;
            seen |= 1u;
        } else if (strcmp(key, "sound") == 0) {
            char text[16];
            if (parse_string(cursor, text, sizeof(text)) != STRING_OK || (seen & 2u) != 0 ||
                parse_sound(text, &out->sound) != PET_MESSAGE_OK) {
                return PET_MESSAGE_ERR_DISPLAY;
            }
            seen |= 2u;
        } else {
            return PET_MESSAGE_ERR_DISPLAY;
        }
        if (!peek(cursor, &next)) {
            return PET_MESSAGE_ERR_DISPLAY;
        }
        if (next == '}') {
            cursor->cur++;
            break;
        }
        if (next != ',' || !consume(cursor, ',')) {
            return PET_MESSAGE_ERR_DISPLAY;
        }
    }
    return PET_MESSAGE_OK;
}

static pet_message_error_t parse_nexus_payload(cursor_t *cursor, pet_message_view_t *out)
{
    if (!consume(cursor, '{'))
        return PET_MESSAGE_ERR_SCHEMA;
    field_mask_t fields = {0};
    bool display_seen = false;
    for (;;) {
        char key[16];
        if (parse_string(cursor, key, sizeof(key)) != STRING_OK || !consume(cursor, ':'))
            return PET_MESSAGE_ERR_SCHEMA;
        if (strcmp(key, "messageId") == 0) {
            if (!mark_field(&fields, 1u << 0) ||
                parse_string(cursor, out->id, sizeof(out->id)) != STRING_OK ||
                !pet_nexus_uuid_valid(out->id))
                return PET_MESSAGE_ERR_ID;
        } else if (strcmp(key, "source") == 0) {
            if (!mark_field(&fields, 1u << 1) ||
                parse_string(cursor, out->source, 33) != STRING_OK ||
                !pet_nexus_token_valid(out->source, 32))
                return PET_MESSAGE_ERR_SOURCE;
        } else if (strcmp(key, "eventType") == 0) {
            char type[65];
            if (!mark_field(&fields, 1u << 2) ||
                parse_string(cursor, type, sizeof(type)) != STRING_OK ||
                !pet_nexus_token_valid(type, 64))
                return PET_MESSAGE_ERR_SCHEMA;
            if (strncmp(type, "comment.", 8) == 0)
                out->template_id = PET_MESSAGE_TEMPLATE_COMMENT_CARD;
            else if (strncmp(type, "alert.", 6) == 0)
                out->template_id = PET_MESSAGE_TEMPLATE_ALERT_CARD;
            else if (strncmp(type, "growth.", 7) == 0)
                out->template_id = PET_MESSAGE_TEMPLATE_GROWTH_CARD;
            else if (strncmp(type, "system.", 7) == 0)
                out->template_id = PET_MESSAGE_TEMPLATE_SYSTEM_CARD;
        } else if (strcmp(key, "title") == 0) {
            if (!mark_field(&fields, 1u << 3) ||
                parse_string(cursor, out->title, 97) != STRING_OK ||
                !nexus_text_valid(out->title))
                return PET_MESSAGE_ERR_TITLE;
        } else if (strcmp(key, "summary") == 0) {
            if (!mark_field(&fields, 1u << 4) ||
                parse_string(cursor, out->body, 181) != STRING_OK ||
                !nexus_text_valid(out->body))
                return PET_MESSAGE_ERR_BODY;
        } else if (strcmp(key, "priority") == 0) {
            char priority[16];
            if (!mark_field(&fields, 1u << 5) ||
                parse_string(cursor, priority, sizeof(priority)) != STRING_OK ||
                parse_priority(priority, &out->priority) != PET_MESSAGE_OK)
                return PET_MESSAGE_ERR_PRIORITY;
        } else if (strcmp(key, "display") == 0) {
            if (display_seen)
                return PET_MESSAGE_ERR_SCHEMA;
            display_seen = true;
            const pet_message_error_t err = parse_nexus_display(cursor, out);
            if (err != PET_MESSAGE_OK)
                return err;
        } else {
            return PET_MESSAGE_ERR_SCHEMA;
        }
        if (consume(cursor, '}'))
            break;
        if (!consume(cursor, ','))
            return PET_MESSAGE_ERR_SCHEMA;
    }
    return fields.seen == 0x3Fu ? PET_MESSAGE_OK : PET_MESSAGE_ERR_SCHEMA;
}

pet_message_error_t pet_message_parse_nexus(const char *json, size_t length,
                                            const char *expected_device,
                                            pet_message_view_t *out)
{
    if (json == NULL || out == NULL || length == 0 || length > PET_NEXUS_PACKET_MAX ||
        !pet_nexus_device_id_valid(expected_device))
        return PET_MESSAGE_ERR_ARG;
    memset(out, 0, sizeof(*out));
    out->duration_ms = 8000;
    out->require_ack = true;
    cursor_t cursor = {.cur = json, .end = json + length};
    field_mask_t fields = {0};
    if (!consume(&cursor, '{'))
        return PET_MESSAGE_ERR_SCHEMA;
    for (;;) {
        char key[16];
        if (parse_string(&cursor, key, sizeof(key)) != STRING_OK || !consume(&cursor, ':'))
            return PET_MESSAGE_ERR_SCHEMA;
        if (strcmp(key, "schemaVersion") == 0) {
            uint32_t version;
            if (!mark_field(&fields, 1u << 0) || !parse_uint(&cursor, &version) || version != 1)
                return PET_MESSAGE_ERR_SCHEMA;
            out->schema_version = (uint16_t)version;
        } else if (strcmp(key, "type") == 0) {
            char type[16];
            if (!mark_field(&fields, 1u << 1) ||
                parse_string(&cursor, type, sizeof(type)) != STRING_OK ||
                strcmp(type, "inbox.message") != 0)
                return PET_MESSAGE_ERR_SCHEMA;
        } else if (strcmp(key, "eventId") == 0) {
            if (!mark_field(&fields, 1u << 2) ||
                parse_string(&cursor, out->delivery.event_id, sizeof(out->delivery.event_id)) != STRING_OK ||
                !pet_nexus_uuid_valid(out->delivery.event_id))
                return PET_MESSAGE_ERR_ID;
        } else if (strcmp(key, "deviceId") == 0) {
            if (!mark_field(&fields, 1u << 3) ||
                parse_string(&cursor, out->delivery.device_id, sizeof(out->delivery.device_id)) != STRING_OK ||
                strcmp(out->delivery.device_id, expected_device) != 0)
                return PET_MESSAGE_ERR_ID;
        } else if (strcmp(key, "sentAt") == 0) {
            if (!mark_field(&fields, 1u << 4) ||
                !parse_uint(&cursor, &out->created_at_epoch_s) || out->created_at_epoch_s == 0)
                return PET_MESSAGE_ERR_TIME;
        } else if (strcmp(key, "expiresAt") == 0) {
            if (!mark_field(&fields, 1u << 5) ||
                !parse_uint(&cursor, &out->expires_at_epoch_s))
                return PET_MESSAGE_ERR_TIME;
        } else if (strcmp(key, "payload") == 0) {
            if (!mark_field(&fields, 1u << 6))
                return PET_MESSAGE_ERR_SCHEMA;
            const pet_message_error_t err = parse_nexus_payload(&cursor, out);
            if (err != PET_MESSAGE_OK)
                return err;
        } else {
            return PET_MESSAGE_ERR_SCHEMA;
        }
        if (consume(&cursor, '}'))
            break;
        if (!consume(&cursor, ','))
            return PET_MESSAGE_ERR_SCHEMA;
    }
    skip_ws(&cursor);
    if (fields.seen != 0x7Fu || cursor.cur != cursor.end)
        return PET_MESSAGE_ERR_SCHEMA;
    return out->expires_at_epoch_s > out->created_at_epoch_s ? PET_MESSAGE_OK
                                                            : PET_MESSAGE_ERR_TIME;
}

int pet_message_nexus_ack_format(char *out, size_t capacity, const char *message_id,
                                 const pet_message_delivery_t *delivery,
                                 pet_message_ack_state_t state, bool expired)
{
    const char *status = ack_state_name(state);
    if (out == NULL || capacity == 0 || delivery == NULL || status == NULL ||
        !pet_nexus_uuid_valid(message_id) || !pet_nexus_uuid_valid(delivery->event_id) ||
        !pet_nexus_device_id_valid(delivery->device_id))
        return -1;
    const int length = snprintf(out, capacity,
        "{\"schemaVersion\":1,\"deviceId\":\"%s\",\"eventId\":\"%s\",\"messageId\":\"%s\","
        "\"status\":\"%s\"%s}", delivery->device_id, delivery->event_id, message_id,
        expired ? "rejected" : status, expired ? ",\"reason\":\"expired\"" : "");
    return length < 0 || (size_t)length >= capacity ? -1 : length;
}
