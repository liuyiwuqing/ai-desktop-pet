#include "message_queue.h"

#include <stddef.h>
#include <string.h>

#include "message_validate.h"

static uint64_t fingerprint(const char *text)
{
    uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
        hash ^= *p;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool is_duplicate(const pet_message_queue_t *queue, const char *id, const char *dedupe_key)
{
    const uint64_t id_hash = fingerprint(id);
    const uint64_t key_hash = dedupe_key[0] != '\0' ? fingerprint(dedupe_key) : 0;
    for (size_t i = 0; i < PET_MESSAGE_DEDUPE_SLOTS; i++) {
        if (queue->dedupe[i] == 0) {
            continue;
        }
        if (queue->dedupe[i] == id_hash || (key_hash != 0 && queue->dedupe[i] == key_hash)) {
            return true;
        }
    }
    return false;
}

static void remember_key(pet_message_queue_t *queue, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    queue->dedupe[queue->dedupe_next] = fingerprint(text);
    queue->dedupe_next = (uint8_t)((queue->dedupe_next + 1U) % PET_MESSAGE_DEDUPE_SLOTS);
}

static bool comes_before(const pet_message_view_t *left, const pet_message_view_t *right)
{
    if (left->priority != right->priority) {
        return left->priority > right->priority;
    }
    return left->created_at_epoch_s < right->created_at_epoch_s;
}

static void insert_sorted(pet_message_queue_t *queue, const pet_message_view_t *message)
{
    uint8_t position = queue->count;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (comes_before(message, &queue->slots[i])) {
            position = i;
            break;
        }
    }
    if (position < queue->count) {
        memmove(&queue->slots[position + 1], &queue->slots[position],
                (size_t)(queue->count - position) * sizeof(queue->slots[0]));
    }
    queue->slots[position] = *message;
    queue->count++;
}

void pet_message_queue_init(pet_message_queue_t *queue, pet_message_view_t *slots, uint8_t capacity)
{
    memset(queue, 0, sizeof(*queue));
    queue->slots = slots;
    queue->capacity = slots != NULL ? capacity : 0;
}

uint8_t pet_message_queue_restore(pet_message_queue_t *queue,
                                  const pet_message_view_t *messages, uint8_t count,
                                  const uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS],
                                  uint8_t dedupe_next, uint32_t now_epoch_s)
{
    if (queue == NULL || queue->slots == NULL || queue->capacity == 0 || messages == NULL ||
        dedupe == NULL) {
        return 0;
    }

    queue->count = 0;
    memcpy(queue->dedupe, dedupe, sizeof(queue->dedupe));
    queue->dedupe_next = (uint8_t)(dedupe_next % PET_MESSAGE_DEDUPE_SLOTS);

    uint8_t restored = 0;
    for (uint8_t i = 0; i < count && restored < queue->capacity; i++) {
        const pet_message_error_t validation = pet_message_validate(&messages[i], now_epoch_s);
        if (validation == PET_MESSAGE_ERR_EXPIRED) {
            queue->expired_total++;
            continue;
        }
        if (validation != PET_MESSAGE_OK) {
            continue;
        }
        insert_sorted(queue, &messages[i]);
        restored++;
    }
    return restored;
}

pet_message_error_t pet_message_queue_push(pet_message_queue_t *queue,
                                           const pet_message_view_t *message,
                                           uint32_t now_epoch_s)
{
    if (queue == NULL || queue->slots == NULL || queue->capacity == 0) {
        return PET_MESSAGE_ERR_ARG;
    }
    if (message == NULL) {
        return PET_MESSAGE_ERR_ARG;
    }

    const pet_message_error_t validation = pet_message_validate(message, now_epoch_s);
    if (validation == PET_MESSAGE_ERR_EXPIRED) {
        queue->expired_total++;
        return validation;
    }
    if (validation != PET_MESSAGE_OK) {
        return validation;
    }

    if (is_duplicate(queue, message->id, message->dedupe_key)) {
        queue->duplicate_total++;
        return PET_MESSAGE_ERR_DUPLICATE;
    }

    if (queue->count == queue->capacity) {
        const pet_message_priority_t lowest = queue->slots[queue->count - 1].priority;
        if (lowest == PET_MESSAGE_PRIORITY_CRITICAL || message->priority < lowest) {
            queue->dropped_total++;
            return PET_MESSAGE_ERR_FULL;
        }

        uint8_t victim = 0;
        while (queue->slots[victim].priority != lowest) {
            victim++;
        }
        memmove(&queue->slots[victim], &queue->slots[victim + 1],
                (size_t)(queue->count - victim - 1U) * sizeof(queue->slots[0]));
        queue->count--;
        queue->dropped_total++;
    }

    insert_sorted(queue, message);
    remember_key(queue, message->id);
    remember_key(queue, message->dedupe_key);
    return PET_MESSAGE_OK;
}

const pet_message_view_t *pet_message_queue_next(const pet_message_queue_t *queue)
{
    if (queue == NULL || queue->count == 0) {
        return NULL;
    }
    return &queue->slots[0];
}

const pet_message_view_t *pet_message_queue_find(const pet_message_queue_t *queue, const char *id)
{
    if (queue == NULL || id == NULL) {
        return NULL;
    }
    for (uint8_t i = 0; i < queue->count; i++) {
        if (strcmp(queue->slots[i].id, id) == 0) {
            return &queue->slots[i];
        }
    }
    return NULL;
}

pet_message_error_t pet_message_queue_ack(pet_message_queue_t *queue, const char *id,
                                          pet_message_ack_state_t state)
{
    if (queue == NULL || id == NULL) {
        return PET_MESSAGE_ERR_ARG;
    }

    uint8_t index = queue->count;
    for (uint8_t i = 0; i < queue->count; i++) {
        if (strcmp(queue->slots[i].id, id) == 0) {
            index = i;
            break;
        }
    }
    if (index == queue->count) {
        return PET_MESSAGE_ERR_ARG;
    }

    pet_message_view_t *message = &queue->slots[index];
    if (!pet_message_ack_transition_allowed(message->state, state)) {
        return PET_MESSAGE_ERR_STATE;
    }
    message->state = state;

    if (state == PET_MESSAGE_STATE_READ) {
        memmove(&queue->slots[index], &queue->slots[index + 1],
                (size_t)(queue->count - index - 1U) * sizeof(queue->slots[0]));
        queue->count--;
    }
    return PET_MESSAGE_OK;
}

uint8_t pet_message_queue_purge_expired(pet_message_queue_t *queue, uint32_t now_epoch_s)
{
    if (queue == NULL) {
        return 0;
    }

    uint8_t removed = 0;
    uint8_t index = 0;
    while (index < queue->count) {
        if (pet_message_expired(&queue->slots[index], now_epoch_s)) {
            memmove(&queue->slots[index], &queue->slots[index + 1],
                    (size_t)(queue->count - index - 1U) * sizeof(queue->slots[0]));
            queue->count--;
            queue->expired_total++;
            removed++;
            continue;
        }
        index++;
    }
    return removed;
}

uint8_t pet_message_queue_count(const pet_message_queue_t *queue)
{
    return queue != NULL ? queue->count : 0;
}
