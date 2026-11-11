#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "message_fixture.h"
#include "message_queue.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

static pet_message_view_t s_slots[PET_MESSAGE_QUEUE_CAPACITY];
static pet_message_queue_t s_queue;

static pet_message_error_t push_message(const char *id, pet_message_priority_t priority,
                                        uint32_t created_at)
{
    pet_message_view_t message;
    message_fixture_init(&message, id, priority, created_at);
    return pet_message_queue_push(&s_queue, &message, created_at);
}

static void test_ordering(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100) == PET_MESSAGE_OK);
    CHECK(push_message("msg_000002", PET_MESSAGE_PRIORITY_HIGH, 200) == PET_MESSAGE_OK);
    CHECK(push_message("msg_000003", PET_MESSAGE_PRIORITY_NORMAL, 150) == PET_MESSAGE_OK);

    const pet_message_view_t *next = pet_message_queue_next(&s_queue);
    CHECK(next != NULL && strcmp(next->id, "msg_000002") == 0);
    CHECK(pet_message_queue_ack(&s_queue, "msg_000002", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);

    next = pet_message_queue_next(&s_queue);
    CHECK(next != NULL && strcmp(next->id, "msg_000001") == 0);
    CHECK(pet_message_queue_ack(&s_queue, "msg_000001", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);

    next = pet_message_queue_next(&s_queue);
    CHECK(next != NULL && strcmp(next->id, "msg_000003") == 0);
    CHECK(pet_message_queue_ack(&s_queue, "msg_000003", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
    CHECK(pet_message_queue_next(&s_queue) == NULL);
    CHECK(pet_message_queue_count(&s_queue) == 0);
}

static void test_duplicate(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100) == PET_MESSAGE_OK);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_HIGH, 200) ==
          PET_MESSAGE_ERR_DUPLICATE);
    CHECK(pet_message_queue_count(&s_queue) == 1);
    CHECK(s_queue.duplicate_total == 1);

    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    pet_message_view_t first;
    message_fixture_init(&first, "msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100);
    strcpy(first.dedupe_key, "halo:comment:1");
    CHECK(pet_message_queue_push(&s_queue, &first, 100) == PET_MESSAGE_OK);

    pet_message_view_t second;
    message_fixture_init(&second, "msg_000002", PET_MESSAGE_PRIORITY_NORMAL, 100);
    strcpy(second.dedupe_key, "halo:comment:1");
    CHECK(pet_message_queue_push(&s_queue, &second, 100) == PET_MESSAGE_ERR_DUPLICATE);
    CHECK(pet_message_queue_count(&s_queue) == 1);
}

static void test_expired(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_000001", PET_MESSAGE_PRIORITY_HIGH, 100);
    message.expires_at_epoch_s = 200;
    CHECK(pet_message_queue_push(&s_queue, &message, 200) == PET_MESSAGE_ERR_EXPIRED);
    CHECK(pet_message_queue_count(&s_queue) == 0);
    CHECK(s_queue.expired_total == 1);
}

static void test_full_eviction(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    char id[PET_MESSAGE_ID_MAX + 1];
    for (unsigned i = 0; i < PET_MESSAGE_QUEUE_CAPACITY; i++) {
        snprintf(id, sizeof(id), "msg_%06u", i);
        CHECK(push_message(id, PET_MESSAGE_PRIORITY_LOW, 100 + i) == PET_MESSAGE_OK);
    }
    CHECK(pet_message_queue_count(&s_queue) == PET_MESSAGE_QUEUE_CAPACITY);

    CHECK(push_message("msg_000008", PET_MESSAGE_PRIORITY_NORMAL, 200) == PET_MESSAGE_OK);
    CHECK(pet_message_queue_count(&s_queue) == PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(s_queue.dropped_total == 1);
    CHECK(pet_message_queue_find(&s_queue, "msg_000000") == NULL);
    CHECK(pet_message_queue_find(&s_queue, "msg_000008") != NULL);
    CHECK(strcmp(pet_message_queue_next(&s_queue)->id, "msg_000008") == 0);
}

static void test_critical_protection(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    char id[PET_MESSAGE_ID_MAX + 1];
    for (unsigned i = 0; i < PET_MESSAGE_QUEUE_CAPACITY; i++) {
        snprintf(id, sizeof(id), "msg_%06u", i);
        CHECK(push_message(id, PET_MESSAGE_PRIORITY_CRITICAL, 100 + i) == PET_MESSAGE_OK);
    }

    CHECK(push_message("msg_000008", PET_MESSAGE_PRIORITY_NORMAL, 200) ==
          PET_MESSAGE_ERR_FULL);
    CHECK(pet_message_queue_count(&s_queue) == PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(s_queue.dropped_total == 1);
    CHECK(pet_message_queue_find(&s_queue, "msg_000000") != NULL);

    CHECK(push_message("msg_000009", PET_MESSAGE_PRIORITY_CRITICAL, 200) ==
          PET_MESSAGE_ERR_FULL);
    CHECK(pet_message_queue_count(&s_queue) == PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(s_queue.dropped_total == 2);
    CHECK(pet_message_queue_find(&s_queue, "msg_000009") == NULL);
}

static void test_full_mixed(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    char id[PET_MESSAGE_ID_MAX + 1];
    for (unsigned i = 0; i < 4; i++) {
        snprintf(id, sizeof(id), "msg_%06u", i);
        CHECK(push_message(id, PET_MESSAGE_PRIORITY_LOW, 100 + i) == PET_MESSAGE_OK);
    }
    for (unsigned i = 4; i < 8; i++) {
        snprintf(id, sizeof(id), "msg_%06u", i);
        CHECK(push_message(id, PET_MESSAGE_PRIORITY_HIGH, 100 + i) == PET_MESSAGE_OK);
    }

    CHECK(push_message("msg_000008", PET_MESSAGE_PRIORITY_NORMAL, 200) == PET_MESSAGE_OK);
    CHECK(pet_message_queue_count(&s_queue) == PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(s_queue.dropped_total == 1);
    CHECK(pet_message_queue_find(&s_queue, "msg_000000") == NULL);
    CHECK(pet_message_queue_find(&s_queue, "msg_000001") != NULL);
    CHECK(strcmp(pet_message_queue_next(&s_queue)->id, "msg_000004") == 0);
}

static void test_ack(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100) == PET_MESSAGE_OK);

    CHECK(pet_message_queue_ack(&s_queue, "msg_000001", PET_MESSAGE_STATE_DISPLAYED) ==
          PET_MESSAGE_OK);
    const pet_message_view_t *message = pet_message_queue_find(&s_queue, "msg_000001");
    CHECK(message != NULL && message->state == PET_MESSAGE_STATE_DISPLAYED);
    CHECK(pet_message_queue_count(&s_queue) == 1);

    CHECK(pet_message_queue_ack(&s_queue, "msg_000001", PET_MESSAGE_STATE_RECEIVED) ==
          PET_MESSAGE_ERR_STATE);
    CHECK(pet_message_queue_ack(&s_queue, "msg_000001", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
    CHECK(pet_message_queue_count(&s_queue) == 0);
    CHECK(pet_message_queue_find(&s_queue, "msg_000001") == NULL);
    CHECK(pet_message_queue_ack(&s_queue, "msg_000001", PET_MESSAGE_STATE_READ) ==
          PET_MESSAGE_ERR_ARG);
    CHECK(pet_message_queue_ack(&s_queue, "msg_999999", PET_MESSAGE_STATE_READ) ==
          PET_MESSAGE_ERR_ARG);
}

static void test_purge_expired(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.expires_at_epoch_s = 200;
    CHECK(pet_message_queue_push(&s_queue, &message, 100) == PET_MESSAGE_OK);

    message_fixture_init(&message, "msg_000002", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.expires_at_epoch_s = 1000;
    CHECK(pet_message_queue_push(&s_queue, &message, 100) == PET_MESSAGE_OK);

    message_fixture_init(&message, "msg_000003", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.expires_at_epoch_s = 0;
    CHECK(pet_message_queue_push(&s_queue, &message, 100) == PET_MESSAGE_OK);

    CHECK(pet_message_queue_purge_expired(&s_queue, 300) == 1);
    CHECK(pet_message_queue_count(&s_queue) == 2);
    CHECK(s_queue.expired_total == 1);

    CHECK(pet_message_queue_purge_expired(&s_queue, 2000) == 1);
    CHECK(pet_message_queue_count(&s_queue) == 1);
    CHECK(s_queue.expired_total == 2);
    CHECK(pet_message_queue_find(&s_queue, "msg_000003") != NULL);
}

static void test_restore(void)
{
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_HIGH, 100) == PET_MESSAGE_OK);

    uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS];
    memcpy(dedupe, s_queue.dedupe, sizeof(dedupe));
    const uint8_t dedupe_next = s_queue.dedupe_next;

    pet_message_view_t restored[3];
    memset(restored, 0, sizeof(restored));
    message_fixture_init(&restored[0], "msg_000001", PET_MESSAGE_PRIORITY_HIGH, 100);
    message_fixture_init(&restored[1], "msg_000002", PET_MESSAGE_PRIORITY_NORMAL, 100);
    restored[1].expires_at_epoch_s = 200;
    message_fixture_init(&restored[2], "bad", PET_MESSAGE_PRIORITY_NORMAL, 100);

    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(pet_message_queue_restore(&s_queue, restored, 3, dedupe, dedupe_next, 300) == 1);
    CHECK(pet_message_queue_count(&s_queue) == 1);
    CHECK(s_queue.expired_total == 1);
    CHECK(strcmp(pet_message_queue_next(&s_queue)->id, "msg_000001") == 0);
    CHECK(pet_message_queue_push(&s_queue, &restored[0], 300) == PET_MESSAGE_ERR_DUPLICATE);
    CHECK(pet_message_queue_restore(NULL, restored, 1, dedupe, dedupe_next, 300) == 0);
}

static void test_init_errors(void)
{
    pet_message_queue_init(&s_queue, NULL, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(pet_message_queue_count(&s_queue) == 0);
    CHECK(pet_message_queue_next(&s_queue) == NULL);
    CHECK(push_message("msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100) == PET_MESSAGE_ERR_ARG);

    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    CHECK(pet_message_queue_push(NULL, NULL, 100) == PET_MESSAGE_ERR_ARG);
    CHECK(pet_message_queue_push(&s_queue, NULL, 100) == PET_MESSAGE_ERR_ARG);
}

int main(void)
{
    test_ordering();
    test_duplicate();
    test_expired();
    test_full_eviction();
    test_critical_protection();
    test_full_mixed();
    test_ack();
    test_purge_expired();
    test_restore();
    test_init_errors();

    if (s_failures == 0) {
        printf("host tests passed: message queue\n");
    }
    return s_failures == 0 ? 0 : 1;
}
