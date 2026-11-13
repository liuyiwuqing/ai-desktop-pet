#include "message_validate.h"

#include <stddef.h>
#include <string.h>

static bool is_lower_alnum(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

/* 有界长度：字段未在容量内终止时返回容量本身，避免越界读取。 */
static size_t bounded_length(const char *text, size_t capacity)
{
    size_t length = 0;
    while (length < capacity && text[length] != '\0') {
        length++;
    }
    return length;
}

bool pet_message_id_valid(const char *id)
{
    if (pet_nexus_uuid_valid(id))
        return true;
    if (id == NULL || strncmp(id, "msg_", 4) != 0) {
        return false;
    }
    const size_t length = strlen(id);
    if (length < 10 || length > PET_MESSAGE_ID_MAX) {
        return false;
    }
    for (size_t i = 4; i < length; i++) {
        if (!is_lower_alnum(id[i]) && !(id[i] >= 'A' && id[i] <= 'Z')) {
            return false;
        }
    }
    return true;
}

bool pet_message_source_valid(const char *source)
{
    if (source == NULL || source[0] == '\0') {
        return false;
    }
    const size_t length = strlen(source);
    if (length > PET_MESSAGE_SOURCE_MAX) {
        return false;
    }

    bool segment_start = true;
    for (size_t i = 0; i < length; i++) {
        const char c = source[i];
        if (c == '.') {
            if (segment_start) {
                return false;
            }
            segment_start = true;
            continue;
        }
        if (segment_start) {
            if (!is_lower_alnum(c)) {
                return false;
            }
            segment_start = false;
        } else if (!is_lower_alnum(c) && c != '-') {
            return false;
        }
    }
    return !segment_start;
}

bool pet_message_template_supported(pet_message_template_t template_id)
{
    return (unsigned)template_id < (unsigned)PET_MESSAGE_TEMPLATE_COUNT;
}

pet_message_template_t pet_message_template_fallback(pet_message_template_t template_id)
{
    return pet_message_template_supported(template_id) ? template_id : PET_MESSAGE_TEMPLATE_TEXT_CARD;
}

pet_event_priority_t pet_message_event_priority(pet_message_priority_t priority)
{
    switch (priority) {
    case PET_MESSAGE_PRIORITY_LOW:
        return PET_EVENT_PRIORITY_LOW;
    case PET_MESSAGE_PRIORITY_HIGH:
        return PET_EVENT_PRIORITY_HIGH;
    case PET_MESSAGE_PRIORITY_CRITICAL:
        return PET_EVENT_PRIORITY_CRITICAL;
    case PET_MESSAGE_PRIORITY_NORMAL:
    default:
        return PET_EVENT_PRIORITY_NORMAL;
    }
}

bool pet_message_expired(const pet_message_view_t *message, uint32_t now_epoch_s)
{
    return message != NULL && message->expires_at_epoch_s != 0 &&
           now_epoch_s >= message->expires_at_epoch_s;
}

pet_message_notify_policy_t pet_message_notify_policy(const pet_message_view_t *message,
                                                      bool dnd_active, bool allow_critical)
{
    pet_message_notify_policy_t policy = {false, false, false};
    if (message == NULL) {
        return policy;
    }

    if (!dnd_active) {
        policy.display = true;
        policy.sound = message->sound != PET_MESSAGE_SOUND_NONE;
        policy.alert_animation = message->priority == PET_MESSAGE_PRIORITY_CRITICAL;
        return policy;
    }

    switch (message->priority) {
    case PET_MESSAGE_PRIORITY_CRITICAL:
        if (allow_critical) {
            policy.display = true;
            policy.sound = message->sound != PET_MESSAGE_SOUND_NONE;
            policy.alert_animation = true;
        }
        break;
    case PET_MESSAGE_PRIORITY_HIGH:
        policy.display = true;
        break;
    case PET_MESSAGE_PRIORITY_NORMAL:
    case PET_MESSAGE_PRIORITY_LOW:
    default:
        break;
    }
    return policy;
}

bool pet_message_ack_transition_allowed(pet_message_ack_state_t from, pet_message_ack_state_t to)
{
    if ((unsigned)from >= (unsigned)PET_MESSAGE_STATE_COUNT ||
        (unsigned)to >= (unsigned)PET_MESSAGE_STATE_COUNT) {
        return false;
    }
    if (from == to) {
        return true;
    }
    if (to == PET_MESSAGE_STATE_DISPLAYED) {
        return from == PET_MESSAGE_STATE_RECEIVED;
    }
    if (to == PET_MESSAGE_STATE_READ) {
        return from == PET_MESSAGE_STATE_RECEIVED || from == PET_MESSAGE_STATE_DISPLAYED;
    }
    return false;
}

pet_message_error_t pet_message_validate(const pet_message_view_t *message, uint32_t now_epoch_s)
{
    if (message == NULL) {
        return PET_MESSAGE_ERR_ARG;
    }
    if (message->schema_version != PET_MESSAGE_SCHEMA_VERSION) {
        return PET_MESSAGE_ERR_SCHEMA;
    }
    if (!pet_message_id_valid(message->id)) {
        return PET_MESSAGE_ERR_ID;
    }
    const bool nexus = message->delivery.event_id[0] != '\0';
    if (nexus) {
        if (!pet_nexus_uuid_valid(message->id) ||
            !pet_nexus_uuid_valid(message->delivery.event_id) ||
            !pet_nexus_device_id_valid(message->delivery.device_id))
            return PET_MESSAGE_ERR_ID;
    } else if (message->delivery.device_id[0] != '\0' || pet_nexus_uuid_valid(message->id)) {
        return PET_MESSAGE_ERR_ID;
    }
    if (nexus ? !pet_nexus_token_valid(message->source, 32)
              : !pet_message_source_valid(message->source)) {
        return PET_MESSAGE_ERR_SOURCE;
    }
    if ((unsigned)message->priority >= (unsigned)PET_MESSAGE_PRIORITY_COUNT) {
        return PET_MESSAGE_ERR_PRIORITY;
    }
    if (bounded_length(message->title, sizeof(message->title)) > PET_MESSAGE_TITLE_MAX) {
        return PET_MESSAGE_ERR_TITLE;
    }
    if (bounded_length(message->body, sizeof(message->body)) > PET_MESSAGE_BODY_MAX) {
        return PET_MESSAGE_ERR_BODY;
    }
    if (message->created_at_epoch_s == 0) {
        return PET_MESSAGE_ERR_TIME;
    }
    if (message->expires_at_epoch_s != 0 &&
        message->expires_at_epoch_s <= message->created_at_epoch_s) {
        return PET_MESSAGE_ERR_TIME;
    }
    if (message->duration_ms < 5000 || message->duration_ms > 30000) {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    if ((unsigned)message->sound >= (unsigned)PET_MESSAGE_SOUND_COUNT) {
        return PET_MESSAGE_ERR_DISPLAY;
    }
    if (pet_message_expired(message, now_epoch_s)) {
        return PET_MESSAGE_ERR_EXPIRED;
    }
    return PET_MESSAGE_OK;
}
