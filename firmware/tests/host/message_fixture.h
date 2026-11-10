#pragma once

#include <string.h>

#include "pet/message.h"

static inline void message_fixture_init(pet_message_view_t *message, const char *id,
                                        pet_message_priority_t priority, uint32_t created_at)
{
    memset(message, 0, sizeof(*message));
    message->schema_version = PET_MESSAGE_SCHEMA_VERSION;
    message->priority = priority;
    message->state = PET_MESSAGE_STATE_RECEIVED;
    message->template_id = PET_MESSAGE_TEMPLATE_TEXT_CARD;
    message->sound = PET_MESSAGE_SOUND_CHIME;
    message->require_ack = true;
    message->created_at_epoch_s = created_at;
    message->expires_at_epoch_s = created_at + 86400U;
    message->duration_ms = 8000;
    strncpy(message->id, id, PET_MESSAGE_ID_MAX);
    message->id[PET_MESSAGE_ID_MAX] = '\0';
    strncpy(message->source, "halo.comment", PET_MESSAGE_SOURCE_MAX);
    message->source[PET_MESSAGE_SOURCE_MAX] = '\0';
    strncpy(message->title, "new comment", PET_MESSAGE_TITLE_MAX);
    message->title[PET_MESSAGE_TITLE_MAX] = '\0';
    strncpy(message->body, "hello", PET_MESSAGE_BODY_MAX);
    message->body[PET_MESSAGE_BODY_MAX] = '\0';
}
