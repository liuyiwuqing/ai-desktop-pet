#include "svc_config/api.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "pet/json_guard.h"

void pet_config_defaults(pet_config_t *out)
{
    if (!out) return;
    *out = (pet_config_t){.schema_version = PET_CFG_SCHEMA_VERSION,
        .language = PET_CFG_DEFAULT_LANGUAGE, .volume = PET_CFG_DEFAULT_VOLUME,
        .brightness = PET_CFG_DEFAULT_BRIGHTNESS, .muted = PET_CFG_DEFAULT_MUTED,
        .dnd = {.enabled = PET_CFG_DND_DEFAULT_ENABLED, .start_minute = PET_CFG_DND_DEFAULT_START_MINUTE,
                .end_minute = PET_CFG_DND_DEFAULT_END_MINUTE, .allow_critical = PET_CFG_DND_DEFAULT_ALLOW_CRITICAL},
        .message = {PET_CFG_MESSAGE_DEFAULT_QUEUE_DEPTH, PET_CFG_MESSAGE_DEFAULT_DURATION_MS}};
    strcpy(out->timezone, PET_CFG_DEFAULT_TIMEZONE);
}

esp_err_t pet_config_utc_offset(const pet_config_t *c, int16_t *minutes)
{
    if (!c || !minutes || !memchr(c->timezone, 0, sizeof(c->timezone))) return ESP_ERR_INVALID_ARG;
    const char *z = c->timezone;
    if (!strcmp(z, "Asia/Shanghai")) { *minutes = 480; return ESP_OK; }
    if (!strcmp(z, "UTC") || !strcmp(z, "Etc/UTC")) { *minutes = 0; return ESP_OK; }
    if (strlen(z) != 9 || strncmp(z, "UTC", 3) || (z[3] != '+' && z[3] != '-') || z[6] != ':')
        return ESP_ERR_INVALID_ARG;
    for (unsigned i = 4; i < 9; ++i)
        if (i != 6 && (z[i] < '0' || z[i] > '9')) return ESP_ERR_INVALID_ARG;
    int h = (z[4]-'0')*10 + z[5]-'0', m = (z[7]-'0')*10 + z[8]-'0';
    int maximum=z[3]=='-'?12:14;
    if (m > 59 || h > maximum || (h == maximum && m)) return ESP_ERR_INVALID_ARG;
    *minutes = (int16_t)((h*60 + m) * (z[3] == '-' ? -1 : 1));
    return ESP_OK;
}

esp_err_t pet_config_validate(const pet_config_t *c)
{
    int16_t offset;
    bool invalid=!c || c->schema_version != PET_CFG_SCHEMA_VERSION ||
        pet_config_utc_offset(c, &offset) != ESP_OK || (unsigned)c->language > PET_CFG_LANGUAGE_EN_US ||
        c->volume > PET_CFG_VOLUME_MAX || c->brightness > PET_CFG_BRIGHTNESS_MAX ||
        c->dnd.start_minute > 1439 || c->dnd.end_minute > 1439 ||
        c->message.queue_depth < 1 || c->message.queue_depth > 8 ||
        c->message.duration_ms < 5000 || c->message.duration_ms > 30000;
    if(invalid)return ESP_ERR_INVALID_ARG;
    /* v1 线契约保留 en-US，但首版 UI 只有中文，拒绝“保存成功却无效果”。 */
    return c->language==PET_CFG_LANGUAGE_EN_US?ESP_ERR_NOT_SUPPORTED:ESP_OK;
}

static bool keys(const cJSON *object, const char *const *names, unsigned count, unsigned required)
{
    if (!cJSON_IsObject(object)) return false;
    unsigned mask = 0;
    for (const cJSON *p = object->child; p; p = p->next) {
        unsigned i = 0;
        while (i < count && strcmp(p->string, names[i])) ++i;
        if (i == count || (mask & (1u << i))) return false;
        mask |= 1u << i;
    }
    return (mask & required) == required;
}
static const cJSON *field(const cJSON *object, const char *name)
{ return cJSON_GetObjectItemCaseSensitive(object, name); }
static bool integer(const cJSON *p, unsigned min, unsigned max)
{ return cJSON_IsNumber(p) && isfinite(p->valuedouble) && p->valuedouble >= min &&
    p->valuedouble <= max && floor(p->valuedouble) == p->valuedouble; }

esp_err_t pet_config_parse_json(const char *json, size_t bytes, pet_config_t *out)
{
    if (!json || !out || !bytes || bytes > PET_CONFIG_JSON_MAX || !pet_json_bounded(json,bytes,3))
        return ESP_ERR_INVALID_ARG;
    char input[PET_CONFIG_JSON_MAX + 1]; memcpy(input, json, bytes); input[bytes] = 0;
    if (strstr(input, "\\u0000")) return ESP_ERR_INVALID_ARG;
    const char *end;
    cJSON *root = cJSON_ParseWithLengthOpts(input, bytes, &end, false);
    while (root && end < input + bytes && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) ++end;
    static const char *const top[] = {"schemaVersion", "timezone", "language", "volume", "brightness", "dnd", "message", "muted"};
    static const char *const dk[] = {"enabled", "startMinute", "endMinute", "allowCritical", "manual"};
    static const char *const mk[] = {"queueDepth", "durationMs"};
    const cJSON *d = field(root, "dnd"), *m = field(root, "message"), *z = field(root, "timezone");
    const cJSON *language = field(root, "language"), *muted = field(root, "muted");
    bool valid = root && end == input + bytes && keys(root, top, 8, 127) &&
        keys(d, dk, 5, 15) && keys(m, mk, 2, 3) && integer(field(root, "schemaVersion"), 1, 1) &&
        cJSON_IsString(z) && strlen(z->valuestring) < sizeof(out->timezone) && cJSON_IsString(language) &&
        (!strcmp(language->valuestring, "zh-CN") || !strcmp(language->valuestring, "en-US")) &&
        integer(field(root, "volume"), 0, 100) && integer(field(root, "brightness"), 0, 100) &&
        (!muted || cJSON_IsBool(muted)) && cJSON_IsBool(field(d, "enabled")) &&
        cJSON_IsBool(field(d, "allowCritical")) && (!field(d, "manual") || cJSON_IsBool(field(d, "manual"))) &&
        integer(field(d, "startMinute"), 0, 1439) &&
        integer(field(d, "endMinute"), 0, 1439) && integer(field(m, "queueDepth"), 1, 8) &&
        integer(field(m, "durationMs"), 5000, 30000);
    pet_config_t candidate;
    esp_err_t validation=ESP_ERR_INVALID_ARG;
    if (valid) {
        pet_config_defaults(&candidate);
        strcpy(candidate.timezone, z->valuestring);
        candidate.language = !strcmp(language->valuestring, "en-US") ? PET_CFG_LANGUAGE_EN_US : PET_CFG_LANGUAGE_ZH_CN;
        candidate.volume = (uint8_t)field(root, "volume")->valueint;
        candidate.brightness = (uint8_t)field(root, "brightness")->valueint;
        candidate.muted = cJSON_IsTrue(muted);
        candidate.dnd.enabled = cJSON_IsTrue(field(d, "enabled"));
        candidate.dnd.manual = cJSON_IsTrue(field(d, "manual"));
        candidate.dnd.allow_critical = cJSON_IsTrue(field(d, "allowCritical"));
        candidate.dnd.start_minute = (uint16_t)field(d, "startMinute")->valueint;
        candidate.dnd.end_minute = (uint16_t)field(d, "endMinute")->valueint;
        candidate.message.queue_depth = (uint8_t)field(m, "queueDepth")->valueint;
        candidate.message.duration_ms = (uint16_t)field(m, "durationMs")->valueint;
        validation=pet_config_validate(&candidate);
    }
    cJSON_Delete(root);
    if (!valid) return ESP_ERR_INVALID_ARG;
    if (validation!=ESP_OK) return validation;
    *out = candidate;
    return ESP_OK;
}

esp_err_t pet_config_format_json(const pet_config_t *c, char *out, size_t capacity, size_t *bytes)
{
    if (!out || !bytes || !capacity || pet_config_validate(c) != ESP_OK) return ESP_ERR_INVALID_ARG;
    int n = snprintf(out, capacity, "{\"schemaVersion\":1,\"timezone\":\"%s\",\"language\":\"%s\","
        "\"volume\":%u,\"brightness\":%u,\"muted\":%s,\"dnd\":{\"enabled\":%s,\"manual\":%s,\"startMinute\":%u,\"endMinute\":%u,"
        "\"allowCritical\":%s},\"message\":{\"queueDepth\":%u,\"durationMs\":%u}}", c->timezone,
        c->language == PET_CFG_LANGUAGE_EN_US ? "en-US" : "zh-CN", c->volume, c->brightness,
        c->muted ? "true" : "false", c->dnd.enabled ? "true" : "false", c->dnd.manual ? "true" : "false", c->dnd.start_minute,
        c->dnd.end_minute, c->dnd.allow_critical ? "true" : "false", c->message.queue_depth, c->message.duration_ms);
    if (n < 0 || (size_t)n >= capacity) return ESP_ERR_INVALID_SIZE;
    *bytes = (size_t)n;
    return ESP_OK;
}

bool pet_config_dnd_active(const pet_config_t *c, uint32_t epoch, bool ready)
{
    if (!c) return true;
    if (c->dnd.manual) return true;
    if (!c->dnd.enabled) return false;
    int16_t offset;
    if (!ready || pet_config_utc_offset(c, &offset) != ESP_OK) return true;
    int64_t local = (int64_t)epoch + (int64_t)offset*60;
    unsigned minute = (unsigned)(((local % 86400 + 86400) % 86400)/60);
    unsigned start = c->dnd.start_minute, end = c->dnd.end_minute;
    return start == end || (start < end ? minute >= start && minute < end : minute >= start || minute < end);
}
