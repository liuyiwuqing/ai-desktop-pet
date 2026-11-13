#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "message_fixture.h"
#include "message_store_codec.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

static uint8_t s_buffer[PET_MESSAGE_STORE_MAX_SIZE];
static pet_message_view_t s_messages[PET_MESSAGE_QUEUE_CAPACITY];
static pet_message_view_t s_decoded[PET_MESSAGE_QUEUE_CAPACITY];
static uint64_t s_dedupe[PET_MESSAGE_DEDUPE_SLOTS];
static uint64_t s_decoded_dedupe[PET_MESSAGE_DEDUPE_SLOTS];

static bool same_message(const pet_message_view_t *left, const pet_message_view_t *right)
{
    return left->schema_version == right->schema_version &&
           left->priority == right->priority && left->state == right->state &&
           left->template_id == right->template_id && left->sound == right->sound &&
           left->require_ack == right->require_ack &&
           left->created_at_epoch_s == right->created_at_epoch_s &&
           left->expires_at_epoch_s == right->expires_at_epoch_s &&
           left->duration_ms == right->duration_ms && strcmp(left->id, right->id) == 0 &&
           strcmp(left->source, right->source) == 0 &&
           strcmp(left->dedupe_key, right->dedupe_key) == 0 &&
           strcmp(left->title, right->title) == 0 && strcmp(left->body, right->body) == 0;
}

static pet_message_store_error_t decode(size_t length, uint8_t capacity)
{
    uint8_t count = 0;
    uint8_t dedupe_next = 0;
    return pet_message_store_decode(s_buffer, length, s_decoded, capacity, &count,
                                    s_decoded_dedupe, &dedupe_next);
}

static void test_sizes(void)
{
    CHECK(pet_message_store_record_size() == PET_MESSAGE_STORE_RECORD_SIZE);
    CHECK(pet_message_store_encoded_size(0) ==
          PET_MESSAGE_STORE_HEADER_SIZE + PET_MESSAGE_DEDUPE_SLOTS * 8u);
    CHECK(pet_message_store_encoded_size(PET_MESSAGE_QUEUE_CAPACITY) ==
          PET_MESSAGE_STORE_MAX_SIZE);
}

static void test_roundtrip(void)
{
    memset(s_messages, 0, sizeof(s_messages));
    message_fixture_init(&s_messages[0], "msg_000001", PET_MESSAGE_PRIORITY_NORMAL, 100);
    strcpy(s_messages[0].dedupe_key, "halo:comment:1");
    strcpy(s_messages[0].title, "新评论");
    strcpy(s_messages[0].body, "hello world");

    message_fixture_init(&s_messages[1], "msg_000002", PET_MESSAGE_PRIORITY_HIGH, 50);
    s_messages[1].state = PET_MESSAGE_STATE_DISPLAYED;
    s_messages[1].template_id = PET_MESSAGE_TEMPLATE_ALERT_CARD;
    s_messages[1].sound = PET_MESSAGE_SOUND_ALERT;
    s_messages[1].require_ack = false;

    for (size_t i = 0; i < PET_MESSAGE_DEDUPE_SLOTS; i++) {
        s_dedupe[i] = 0x1122334455667700ULL + i;
    }

    size_t length = 0;
    CHECK(pet_message_store_encode(s_messages, 2, s_dedupe, 5, s_buffer, sizeof(s_buffer),
                                   &length) == PET_MESSAGE_STORE_OK);
    CHECK(length == pet_message_store_encoded_size(2));

    memset(s_decoded, 0, sizeof(s_decoded));
    uint8_t count = 0;
    uint8_t dedupe_next = 0;
    CHECK(pet_message_store_decode(s_buffer, length, s_decoded, PET_MESSAGE_QUEUE_CAPACITY,
                                   &count, s_decoded_dedupe, &dedupe_next) ==
          PET_MESSAGE_STORE_OK);
    CHECK(count == 2);
    CHECK(dedupe_next == 5);
    CHECK(memcmp(s_dedupe, s_decoded_dedupe, sizeof(s_dedupe)) == 0);
    CHECK(same_message(&s_messages[0], &s_decoded[0]));
    CHECK(same_message(&s_messages[1], &s_decoded[1]));
}

static void test_empty(void)
{
    size_t length = 0;
    CHECK(pet_message_store_encode(NULL, 0, s_dedupe, 0, s_buffer, sizeof(s_buffer), &length) ==
          PET_MESSAGE_STORE_OK);
    CHECK(length == pet_message_store_encoded_size(0));

    uint8_t count = 0xFF;
    uint8_t dedupe_next = 0xFF;
    CHECK(pet_message_store_decode(s_buffer, length, s_decoded, PET_MESSAGE_QUEUE_CAPACITY,
                                   &count, s_decoded_dedupe, &dedupe_next) ==
          PET_MESSAGE_STORE_OK);
    CHECK(count == 0);
    CHECK(dedupe_next == 0);
    CHECK(memcmp(s_dedupe, s_decoded_dedupe, sizeof(s_dedupe)) == 0);
}

static void test_corrupt(void)
{
    memset(s_messages, 0, sizeof(s_messages));
    message_fixture_init(&s_messages[0], "msg_000003", PET_MESSAGE_PRIORITY_NORMAL, 100);
    size_t length = 0;
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, 0, s_buffer, sizeof(s_buffer),
                                   &length) == PET_MESSAGE_STORE_OK);

    uint8_t saved = s_buffer[0];
    s_buffer[0] = (uint8_t)(saved ^ 0xFFu);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_MAGIC);
    s_buffer[0] = saved;

    saved = s_buffer[4];
    s_buffer[4] = (uint8_t)(saved + 1u);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_VERSION);
    s_buffer[4] = saved;

    saved = s_buffer[6];
    s_buffer[6] = (uint8_t)(saved + 1u);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_RECORD);
    s_buffer[6] = saved;

    const size_t payload_start =
        PET_MESSAGE_STORE_HEADER_SIZE + PET_MESSAGE_DEDUPE_SLOTS * 8u + 20u;
    saved = s_buffer[payload_start];
    s_buffer[payload_start] = (uint8_t)(saved ^ 0x01u);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_CRC);
    s_buffer[payload_start] = saved;

    CHECK(decode(length - 1u, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_SIZE);
    CHECK(decode(length + 1u, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_SIZE);
    CHECK(decode(PET_MESSAGE_STORE_HEADER_SIZE - 1u, PET_MESSAGE_QUEUE_CAPACITY) ==
          PET_MESSAGE_STORE_ERR_SIZE);
    CHECK(decode(length, 0) == PET_MESSAGE_STORE_ERR_SIZE);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_OK);
}

static void test_encode_errors(void)
{
    memset(s_messages, 0, sizeof(s_messages));
    message_fixture_init(&s_messages[0], "msg_000004", PET_MESSAGE_PRIORITY_NORMAL, 100);

    size_t length = 0;
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, 0, s_buffer,
                                   pet_message_store_encoded_size(1) - 1u, &length) ==
          PET_MESSAGE_STORE_ERR_SIZE);
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, PET_MESSAGE_DEDUPE_SLOTS, s_buffer,
                                   sizeof(s_buffer), &length) == PET_MESSAGE_STORE_ERR_ARG);
    CHECK(pet_message_store_encode(NULL, 1, s_dedupe, 0, s_buffer, sizeof(s_buffer), &length) ==
          PET_MESSAGE_STORE_ERR_ARG);
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, 0, s_buffer, sizeof(s_buffer), NULL) ==
          PET_MESSAGE_STORE_ERR_ARG);
    CHECK(pet_message_store_encode(s_messages, PET_MESSAGE_QUEUE_CAPACITY + 1u, s_dedupe, 0,
                                   s_buffer, sizeof(s_buffer),
                                   &length) == PET_MESSAGE_STORE_ERR_ARG);
}

static void test_invalid_record(void)
{
    memset(s_messages, 0, sizeof(s_messages));
    message_fixture_init(&s_messages[0], "msg_000005", PET_MESSAGE_PRIORITY_NORMAL, 100);
    s_messages[0].priority = PET_MESSAGE_PRIORITY_COUNT;

    size_t length = 0;
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, 0, s_buffer, sizeof(s_buffer),
                                   &length) == PET_MESSAGE_STORE_OK);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_RECORD);

    message_fixture_init(&s_messages[0], "msg_000005", PET_MESSAGE_PRIORITY_NORMAL, 100);
    s_messages[0].template_id = PET_MESSAGE_TEMPLATE_COUNT;
    CHECK(pet_message_store_encode(s_messages, 1, s_dedupe, 0, s_buffer, sizeof(s_buffer),
                                   &length) == PET_MESSAGE_STORE_OK);
    CHECK(decode(length, PET_MESSAGE_QUEUE_CAPACITY) == PET_MESSAGE_STORE_ERR_RECORD);
}

static void test_durable_receipts_and_legacy_snapshot(void)
{
    static pet_message_snapshot_t snapshot;
    static pet_message_snapshot_t restored;
    static uint8_t bytes[PET_MESSAGE_SNAPSHOT_MAX_SIZE];
    message_fixture_init(&snapshot.messages[0], "msg_legacy1", PET_MESSAGE_PRIORITY_NORMAL, 100);
    snapshot.count = 1;
    snapshot.ack_count = 1;
    strcpy(snapshot.acks[0].id, "11111111-2222-4333-8444-555555555555");
    strcpy(snapshot.acks[0].delivery.event_id, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
    strcpy(snapshot.acks[0].delivery.device_id, "dodo-01");
    snapshot.acks[0].state = PET_MESSAGE_STATE_READ;
    snapshot.acks[0].at_epoch_s = 110;
    snapshot.acks[0].queue_depth = 1;
    size_t length = 0;
    CHECK(pet_message_snapshot_encode(&snapshot, bytes, sizeof(bytes), &length) ==
          PET_MESSAGE_STORE_OK);
    CHECK(pet_message_snapshot_decode(bytes, length, &restored) == PET_MESSAGE_STORE_OK);
    CHECK(restored.count == 1 && restored.ack_count == 1);
    CHECK(strcmp(restored.acks[0].id, snapshot.acks[0].id) == 0);
    CHECK(strcmp(restored.acks[0].delivery.event_id, snapshot.acks[0].delivery.event_id) == 0);
    CHECK(strcmp(restored.acks[0].delivery.device_id, "dodo-01") == 0);
    CHECK(restored.acks[0].state == PET_MESSAGE_STATE_READ);
    CHECK(restored.acks[0].at_epoch_s == 110 && restored.acks[0].queue_depth == 1);
    bytes[length - 1] ^= 1;
    CHECK(pet_message_snapshot_decode(bytes, length, &restored) == PET_MESSAGE_STORE_ERR_CRC);
    CHECK(pet_message_snapshot_decode(bytes, length - 1, &restored) != PET_MESSAGE_STORE_OK);

    CHECK(pet_message_store_encode(snapshot.messages, 1, snapshot.dedupe, 0, bytes, sizeof(bytes),
                                   &length) == PET_MESSAGE_STORE_OK);
    CHECK(pet_message_snapshot_decode(bytes, length, &restored) == PET_MESSAGE_STORE_OK);
    CHECK(restored.count == 1 && restored.ack_count == 0);
    CHECK(strcmp(restored.messages[0].id, "msg_legacy1") == 0);
    snapshot.acks[0].state = PET_MESSAGE_STATE_COUNT;
    CHECK(pet_message_snapshot_encode(&snapshot, bytes, sizeof(bytes), &length) ==
          PET_MESSAGE_STORE_ERR_RECORD);
}

static void test_v3_routing_and_migration(void)
{
    static pet_message_snapshot_t snapshot;
    static pet_message_snapshot_t restored;
    static uint8_t bytes[PET_MESSAGE_SNAPSHOT_MAX_SIZE];
    const char *fixtures[] = {"message-snapshot-v1.bin", "message-snapshot-v2.bin"};
    for (unsigned version = 1; version <= 2; ++version) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", TEST_FIXTURE_DIR, fixtures[version - 1]);
        FILE *file = fopen(path, "rb");
        CHECK(file != NULL);
        if (file == NULL)
            return;
        size_t length = fread(bytes, 1, sizeof(bytes), file);
        fclose(file);
        CHECK(pet_message_snapshot_decode(bytes, length, &snapshot) == PET_MESSAGE_STORE_OK);
        CHECK(snapshot.count == 1 && snapshot.ack_count == 0);
        CHECK(strcmp(snapshot.messages[0].id, "msg_legacy1") == 0);
        CHECK(snapshot.messages[0].delivery.event_id[0] == 0);
        CHECK(snapshot.legacy_ack_count == version - 1);
        if (version == 2) {
            CHECK(strcmp(snapshot.legacy_acks[0].id, "msg_oldread") == 0);
            CHECK(snapshot.legacy_acks[0].state == PET_MESSAGE_STATE_READ);
        }
        message_fixture_init(&snapshot.messages[1], "11111111-2222-4333-8444-555555555555",
                              PET_MESSAGE_PRIORITY_HIGH, 1794362400);
        strcpy(snapshot.messages[1].delivery.event_id, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
        strcpy(snapshot.messages[1].delivery.device_id, "dodo-01");
        snapshot.count = 2;
        snapshot.ack_count = 1;
        strcpy(snapshot.acks[0].id, snapshot.messages[1].id);
        snapshot.acks[0].delivery = snapshot.messages[1].delivery;
        snapshot.acks[0].state = PET_MESSAGE_STATE_READ;
        snapshot.acks[0].expired = true;
        CHECK(pet_message_snapshot_encode(&snapshot, bytes, sizeof(bytes), &length) == PET_MESSAGE_STORE_OK);
        CHECK(pet_message_snapshot_decode(bytes, length, &restored) == PET_MESSAGE_STORE_OK);
        CHECK(restored.count == 2 && restored.ack_count == 1 && restored.legacy_ack_count == version - 1);
        CHECK(strcmp(restored.messages[1].delivery.event_id, snapshot.messages[1].delivery.event_id) == 0);
        CHECK(strcmp(restored.messages[1].delivery.device_id, "dodo-01") == 0);
        CHECK(restored.acks[0].expired);
        CHECK(memcmp(restored.legacy_acks, snapshot.legacy_acks, sizeof(snapshot.legacy_acks)) == 0);
        snapshot.acks[0].delivery.event_id[0] = 0;
        CHECK(pet_message_snapshot_encode(&snapshot, bytes, sizeof(bytes), &length) == PET_MESSAGE_STORE_ERR_RECORD);
    }
}

int main(void)
{
    test_sizes();
    test_roundtrip();
    test_empty();
    test_corrupt();
    test_encode_errors();
    test_invalid_record();
    test_durable_receipts_and_legacy_snapshot();
    test_v3_routing_and_migration();

    if (s_failures == 0) {
        printf("host tests passed: message store codec\n");
    }
    return s_failures == 0 ? 0 : 1;
}
