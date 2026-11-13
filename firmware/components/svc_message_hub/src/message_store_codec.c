#include "message_store_codec.h"

#include <string.h>

/* 记录内固定字段：2+6×u8+3×u32，后接 5 个定长字符串。 */
#define RECORD_FIXED_SIZE 20u

static uint16_t read_u16(const uint8_t *src)
{
    return (uint16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static uint32_t read_u32(const uint8_t *src)
{
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static void write_u16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void write_u32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return crc;
}

static void write_fixed_string(uint8_t *dst, size_t field_size, const char *text)
{
    memset(dst, 0, field_size);
    if (text == NULL) {
        return;
    }
    size_t length = 0;
    while (length < field_size - 1u && text[length] != '\0') {
        ++length;
    }
    memcpy(dst, text, length);
}

static bool read_fixed_string(const uint8_t *src, size_t field_size, char *dst, size_t dst_size)
{
    if (dst_size != field_size || memchr(src, '\0', field_size) == NULL) {
        return false;
    }
    memcpy(dst, src, field_size);
    return true;
}

size_t pet_message_store_record_size(void)
{
    return PET_MESSAGE_STORE_RECORD_SIZE;
}

size_t pet_message_store_encoded_size(uint8_t count)
{
    return PET_MESSAGE_STORE_HEADER_SIZE +
           (size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u +
           (size_t)count * PET_MESSAGE_STORE_RECORD_SIZE;
}

pet_message_store_error_t pet_message_store_encode(
    const pet_message_view_t *messages, uint8_t count,
    const uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS], uint8_t dedupe_next,
    uint8_t *out, size_t capacity, size_t *out_length)
{
    if (out == NULL || out_length == NULL || dedupe == NULL ||
        count > PET_MESSAGE_QUEUE_CAPACITY || dedupe_next >= PET_MESSAGE_DEDUPE_SLOTS ||
        (count > 0 && messages == NULL)) {
        return PET_MESSAGE_STORE_ERR_ARG;
    }

    const size_t encoded_size = pet_message_store_encoded_size(count);
    if (capacity < encoded_size) {
        return PET_MESSAGE_STORE_ERR_SIZE;
    }

    const uint32_t payload_length =
        (uint32_t)((size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u +
                   (size_t)count * PET_MESSAGE_STORE_RECORD_SIZE);

    write_u32(&out[0], PET_MESSAGE_STORE_MAGIC);
    write_u16(&out[4], PET_MESSAGE_STORE_LAYOUT_VERSION);
    write_u16(&out[6], (uint16_t)PET_MESSAGE_STORE_RECORD_SIZE);
    out[8] = count;
    out[9] = dedupe_next;
    write_u16(&out[10], 0);
    write_u32(&out[12], payload_length);
    write_u32(&out[16], 0);

    uint8_t *cursor = &out[PET_MESSAGE_STORE_HEADER_SIZE];
    for (size_t i = 0; i < PET_MESSAGE_DEDUPE_SLOTS; i++) {
        write_u32(&cursor[i * 8u], (uint32_t)(dedupe[i] & 0xFFFFFFFFu));
        write_u32(&cursor[i * 8u + 4u], (uint32_t)(dedupe[i] >> 32));
    }
    cursor += (size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u;

    for (uint8_t i = 0; i < count; i++) {
        const pet_message_view_t *message = &messages[i];
        write_u16(&cursor[0], message->schema_version);
        cursor[2] = (uint8_t)message->priority;
        cursor[3] = (uint8_t)message->state;
        cursor[4] = (uint8_t)message->template_id;
        cursor[5] = (uint8_t)message->sound;
        cursor[6] = message->require_ack ? 1u : 0u;
        cursor[7] = 0;
        write_u32(&cursor[8], message->created_at_epoch_s);
        write_u32(&cursor[12], message->expires_at_epoch_s);
        write_u32(&cursor[16], message->duration_ms);
        write_fixed_string(&cursor[20], PET_MESSAGE_ID_MAX + 1u, message->id);
        write_fixed_string(&cursor[20 + PET_MESSAGE_ID_MAX + 1u], PET_MESSAGE_SOURCE_MAX + 1u,
                           message->source);
        write_fixed_string(&cursor[20 + (PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u)],
                           PET_MESSAGE_DEDUPE_KEY_MAX + 1u, message->dedupe_key);
        write_fixed_string(&cursor[20 + (PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u) +
                                    (PET_MESSAGE_DEDUPE_KEY_MAX + 1u)],
                           PET_MESSAGE_TITLE_MAX + 1u, message->title);
        write_fixed_string(&cursor[20 + (PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u) +
                                    (PET_MESSAGE_DEDUPE_KEY_MAX + 1u) +
                                    (PET_MESSAGE_TITLE_MAX + 1u)],
                           PET_MESSAGE_BODY_MAX + 1u, message->body);
        cursor += PET_MESSAGE_STORE_RECORD_SIZE;
    }

    uint32_t crc = crc32_update(0xFFFFFFFFu, out, PET_MESSAGE_STORE_HEADER_SIZE - 4u);
    crc = crc32_update(crc, &out[PET_MESSAGE_STORE_HEADER_SIZE], payload_length) ^ 0xFFFFFFFFu;
    write_u32(&out[16], crc);

    *out_length = encoded_size;
    return PET_MESSAGE_STORE_OK;
}

#define SNAPSHOT_V2_MAGIC 0x32534D50u /* PMS2 */
#define SNAPSHOT_V3_MAGIC 0x33534D50u /* PMS3 */

static bool valid_delivery(const pet_message_delivery_t *delivery, const char *id, bool legacy)
{
    if (legacy)
        return strncmp(id, "msg_", 4) == 0 && pet_message_id_valid(id) &&
               delivery->event_id[0] == 0 && delivery->device_id[0] == 0;
    return pet_nexus_uuid_valid(id) && pet_nexus_uuid_valid(delivery->event_id) &&
           pet_nexus_device_id_valid(delivery->device_id);
}

static bool valid_ack(const pet_message_pending_ack_t *ack, bool legacy)
{
    return memchr(ack->id, '\0', sizeof(ack->id)) != NULL &&
           valid_delivery(&ack->delivery, ack->id, legacy) &&
           (unsigned)ack->state < PET_MESSAGE_STATE_COUNT &&
           ack->queue_depth <= PET_MESSAGE_QUEUE_CAPACITY &&
           (!(ack->expired || ack->confirmed) || ack->state == PET_MESSAGE_STATE_READ) &&
           (!legacy || (!ack->expired && !ack->confirmed));
}

static bool valid_ack_list(const pet_message_pending_ack_t *acks, uint8_t count, bool legacy)
{
    for (uint8_t i = 0; i < count; ++i) {
        if (!valid_ack(&acks[i], legacy))
            return false;
        for (uint8_t j = 0; j < i; ++j)
            if (strcmp(acks[i].id, acks[j].id) == 0)
                return false;
    }
    return true;
}

static void write_delivery(uint8_t *out, const pet_message_delivery_t *delivery)
{
    write_fixed_string(out, PET_NEXUS_UUID_LENGTH + 1u, delivery->event_id);
    write_fixed_string(out + PET_NEXUS_UUID_LENGTH + 1u,
                       PET_NEXUS_DEVICE_ID_MAX + 1u, delivery->device_id);
}

static bool read_delivery(const uint8_t *data, pet_message_delivery_t *out)
{
    return read_fixed_string(data, PET_NEXUS_UUID_LENGTH + 1u, out->event_id, sizeof(out->event_id)) &&
           read_fixed_string(data + PET_NEXUS_UUID_LENGTH + 1u, PET_NEXUS_DEVICE_ID_MAX + 1u,
                              out->device_id, sizeof(out->device_id));
}

static void write_ack(uint8_t *out, const pet_message_pending_ack_t *ack)
{
    write_fixed_string(out, PET_MESSAGE_ID_MAX + 1u, ack->id);
    out[PET_MESSAGE_ID_MAX + 1u] = (uint8_t)ack->state;
    write_u32(out + PET_MESSAGE_ID_MAX + 2u, ack->at_epoch_s);
    out[PET_MESSAGE_ID_MAX + 6u] = ack->queue_depth;
}

static bool read_ack(const uint8_t *data, pet_message_pending_ack_t *out)
{
    if (!read_fixed_string(data, PET_MESSAGE_ID_MAX + 1u, out->id, sizeof(out->id)))
        return false;
    out->state = (pet_message_ack_state_t)data[PET_MESSAGE_ID_MAX + 1u];
    out->at_epoch_s = read_u32(data + PET_MESSAGE_ID_MAX + 2u);
    out->queue_depth = data[PET_MESSAGE_ID_MAX + 6u];
    return true;
}

pet_message_store_error_t pet_message_snapshot_encode(const pet_message_snapshot_t *snapshot,
                                                      uint8_t *out, size_t capacity, size_t *length)
{
    if (snapshot == NULL || out == NULL || length == NULL ||
        snapshot->count > PET_MESSAGE_QUEUE_CAPACITY ||
        snapshot->ack_count > PET_MESSAGE_ACK_CAPACITY ||
        snapshot->legacy_ack_count > PET_MESSAGE_ACK_CAPACITY)
        return PET_MESSAGE_STORE_ERR_ARG;
    const size_t inbox_size = pet_message_store_encoded_size(snapshot->count);
    const size_t total = PET_MESSAGE_SNAPSHOT_HEADER_SIZE + inbox_size +
                         snapshot->count * PET_MESSAGE_DELIVERY_RECORD_SIZE +
                         snapshot->ack_count * PET_MESSAGE_NEXUS_ACK_RECORD_SIZE +
                         snapshot->legacy_ack_count * PET_MESSAGE_ACK_RECORD_SIZE;
    if (capacity < total)
        return PET_MESSAGE_STORE_ERR_SIZE;
    if (!valid_ack_list(snapshot->acks, snapshot->ack_count, false) ||
        !valid_ack_list(snapshot->legacy_acks, snapshot->legacy_ack_count, true))
        return PET_MESSAGE_STORE_ERR_RECORD;
    for (uint8_t i = 0; i < snapshot->count; ++i) {
        const pet_message_view_t *message = &snapshot->messages[i];
        if (memchr(message->id, '\0', sizeof(message->id)) == NULL ||
            !valid_delivery(&message->delivery, message->id, message->delivery.event_id[0] == 0))
            return PET_MESSAGE_STORE_ERR_RECORD;
    }
    size_t encoded = 0;
    pet_message_store_error_t err =
        pet_message_store_encode(snapshot->messages, snapshot->count, snapshot->dedupe,
                                 snapshot->dedupe_next, out + PET_MESSAGE_SNAPSHOT_HEADER_SIZE,
                                 capacity - PET_MESSAGE_SNAPSHOT_HEADER_SIZE, &encoded);
    if (err != PET_MESSAGE_STORE_OK)
        return err;
    write_u32(out, SNAPSHOT_V3_MAGIC);
    write_u16(out + 4, 3);
    out[6] = snapshot->ack_count;
    out[7] = snapshot->legacy_ack_count;
    write_u32(out + 8, (uint32_t)encoded);
    uint8_t *cursor = out + PET_MESSAGE_SNAPSHOT_HEADER_SIZE + encoded;
    for (uint8_t i = 0; i < snapshot->count; ++i) {
        write_delivery(cursor, &snapshot->messages[i].delivery);
        cursor += PET_MESSAGE_DELIVERY_RECORD_SIZE;
    }
    for (uint8_t i = 0; i < snapshot->ack_count; ++i) {
        const pet_message_pending_ack_t *ack = &snapshot->acks[i];
        write_ack(cursor, ack);
        write_delivery(cursor + PET_MESSAGE_ACK_RECORD_SIZE, &ack->delivery);
        cursor[PET_MESSAGE_NEXUS_ACK_RECORD_SIZE - 1] =
            (ack->expired ? 1u : 0u) | (ack->confirmed ? 2u : 0u);
        cursor += PET_MESSAGE_NEXUS_ACK_RECORD_SIZE;
    }
    for (uint8_t i = 0; i < snapshot->legacy_ack_count; ++i) {
        write_ack(cursor, &snapshot->legacy_acks[i]);
        cursor += PET_MESSAGE_ACK_RECORD_SIZE;
    }
    uint32_t crc = crc32_update(0xFFFFFFFFu, out, 12);
    crc = crc32_update(crc, out + PET_MESSAGE_SNAPSHOT_HEADER_SIZE,
                       total - PET_MESSAGE_SNAPSHOT_HEADER_SIZE) ^ 0xFFFFFFFFu;
    write_u32(out + 12, crc);
    *length = total;
    return PET_MESSAGE_STORE_OK;
}

pet_message_store_error_t pet_message_snapshot_decode(const uint8_t *data, size_t length,
                                                      pet_message_snapshot_t *out)
{
    if (data == NULL || out == NULL)
        return PET_MESSAGE_STORE_ERR_ARG;
    memset(out, 0, sizeof(*out));
    if (length < PET_MESSAGE_SNAPSHOT_HEADER_SIZE)
        return PET_MESSAGE_STORE_ERR_SIZE;
    if (read_u32(data) == PET_MESSAGE_STORE_MAGIC)
        return pet_message_store_decode(data, length, out->messages, PET_MESSAGE_QUEUE_CAPACITY,
                                        &out->count, out->dedupe, &out->dedupe_next);
    const bool legacy = read_u32(data) == SNAPSHOT_V2_MAGIC;
    if (!legacy && read_u32(data) != SNAPSHOT_V3_MAGIC)
        return PET_MESSAGE_STORE_ERR_MAGIC;
    if (read_u16(data + 4) != (legacy ? 2 : 3))
        return PET_MESSAGE_STORE_ERR_VERSION;
    const uint8_t count = data[6];
    const uint8_t legacy_count = legacy ? count : data[7];
    const uint32_t inbox_size = read_u32(data + 8);
    if ((legacy && data[7] != 0) || count > PET_MESSAGE_ACK_CAPACITY ||
        legacy_count > PET_MESSAGE_ACK_CAPACITY || inbox_size > PET_MESSAGE_STORE_MAX_SIZE)
        return PET_MESSAGE_STORE_ERR_RECORD;
    if (length < PET_MESSAGE_SNAPSHOT_HEADER_SIZE + (size_t)inbox_size)
        return PET_MESSAGE_STORE_ERR_SIZE;
    uint32_t crc = crc32_update(0xFFFFFFFFu, data, 12);
    crc = crc32_update(crc, data + PET_MESSAGE_SNAPSHOT_HEADER_SIZE,
                       length - PET_MESSAGE_SNAPSHOT_HEADER_SIZE) ^ 0xFFFFFFFFu;
    if (crc != read_u32(data + 12))
        return PET_MESSAGE_STORE_ERR_CRC;
    pet_message_store_error_t err = pet_message_store_decode(
        data + PET_MESSAGE_SNAPSHOT_HEADER_SIZE, inbox_size, out->messages,
        PET_MESSAGE_QUEUE_CAPACITY, &out->count, out->dedupe, &out->dedupe_next);
    if (err != PET_MESSAGE_STORE_OK)
        return err;
    const size_t expected = PET_MESSAGE_SNAPSHOT_HEADER_SIZE + (size_t)inbox_size +
        legacy_count * PET_MESSAGE_ACK_RECORD_SIZE +
        (legacy ? 0u : out->count * PET_MESSAGE_DELIVERY_RECORD_SIZE +
                       count * PET_MESSAGE_NEXUS_ACK_RECORD_SIZE);
    if (length != expected)
        return PET_MESSAGE_STORE_ERR_SIZE;
    const uint8_t *cursor = data + PET_MESSAGE_SNAPSHOT_HEADER_SIZE + inbox_size;
    if (!legacy) {
        for (uint8_t i = 0; i < out->count; ++i) {
            pet_message_view_t *message = &out->messages[i];
            if (!read_delivery(cursor, &message->delivery) ||
                !valid_delivery(&message->delivery, message->id, message->delivery.event_id[0] == 0))
                return PET_MESSAGE_STORE_ERR_RECORD;
            cursor += PET_MESSAGE_DELIVERY_RECORD_SIZE;
        }
        for (uint8_t i = 0; i < count; ++i) {
            pet_message_pending_ack_t *ack = &out->acks[i];
            if (!read_ack(cursor, ack) || !read_delivery(cursor + PET_MESSAGE_ACK_RECORD_SIZE, &ack->delivery))
                return PET_MESSAGE_STORE_ERR_RECORD;
            const uint8_t flags = cursor[PET_MESSAGE_NEXUS_ACK_RECORD_SIZE - 1];
            if (flags > 3)
                return PET_MESSAGE_STORE_ERR_RECORD;
            ack->expired = (flags & 1) != 0;
            ack->confirmed = (flags & 2) != 0;
            cursor += PET_MESSAGE_NEXUS_ACK_RECORD_SIZE;
        }
        out->ack_count = count;
    }
    for (uint8_t i = 0; i < legacy_count; ++i) {
        if (!read_ack(cursor, &out->legacy_acks[i]))
            return PET_MESSAGE_STORE_ERR_RECORD;
        cursor += PET_MESSAGE_ACK_RECORD_SIZE;
    }
    out->legacy_ack_count = legacy_count;
    return valid_ack_list(out->acks, out->ack_count, false) &&
           valid_ack_list(out->legacy_acks, out->legacy_ack_count, true)
               ? PET_MESSAGE_STORE_OK : PET_MESSAGE_STORE_ERR_RECORD;
}

pet_message_store_error_t pet_message_store_decode(const uint8_t *data, size_t length,
                                                   pet_message_view_t *messages, uint8_t capacity,
                                                   uint8_t *count,
                                                   uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS],
                                                   uint8_t *dedupe_next)
{
    if (data == NULL || messages == NULL || count == NULL || dedupe == NULL ||
        dedupe_next == NULL) {
        return PET_MESSAGE_STORE_ERR_ARG;
    }
    if (length < PET_MESSAGE_STORE_HEADER_SIZE) {
        return PET_MESSAGE_STORE_ERR_SIZE;
    }
    if (read_u32(&data[0]) != PET_MESSAGE_STORE_MAGIC) {
        return PET_MESSAGE_STORE_ERR_MAGIC;
    }
    if (read_u16(&data[4]) != PET_MESSAGE_STORE_LAYOUT_VERSION) {
        return PET_MESSAGE_STORE_ERR_VERSION;
    }
    if (read_u16(&data[6]) != (uint16_t)PET_MESSAGE_STORE_RECORD_SIZE) {
        return PET_MESSAGE_STORE_ERR_RECORD;
    }

    const uint8_t stored_count = data[8];
    const uint8_t stored_dedupe_next = data[9];
    if (stored_count > PET_MESSAGE_QUEUE_CAPACITY ||
        stored_dedupe_next >= PET_MESSAGE_DEDUPE_SLOTS) {
        return PET_MESSAGE_STORE_ERR_RECORD;
    }
    if (stored_count > capacity) {
        return PET_MESSAGE_STORE_ERR_SIZE;
    }

    const uint32_t payload_length = read_u32(&data[12]);
    const uint32_t expected_payload =
        (uint32_t)((size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u +
                   (size_t)stored_count * PET_MESSAGE_STORE_RECORD_SIZE);
    if (payload_length != expected_payload) {
        return PET_MESSAGE_STORE_ERR_RECORD;
    }
    if (length != PET_MESSAGE_STORE_HEADER_SIZE + (size_t)payload_length) {
        return PET_MESSAGE_STORE_ERR_SIZE;
    }

    uint32_t crc = crc32_update(0xFFFFFFFFu, data, PET_MESSAGE_STORE_HEADER_SIZE - 4u);
    crc = crc32_update(crc, &data[PET_MESSAGE_STORE_HEADER_SIZE], payload_length) ^ 0xFFFFFFFFu;
    if (crc != read_u32(&data[16])) {
        return PET_MESSAGE_STORE_ERR_CRC;
    }

    const uint8_t *cursor = &data[PET_MESSAGE_STORE_HEADER_SIZE];
    for (size_t i = 0; i < PET_MESSAGE_DEDUPE_SLOTS; i++) {
        dedupe[i] = (uint64_t)read_u32(&cursor[i * 8u]) |
                    ((uint64_t)read_u32(&cursor[i * 8u + 4u]) << 32);
    }
    cursor += (size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u;

    for (uint8_t i = 0; i < stored_count; i++) {
        pet_message_view_t message;
        memset(&message, 0, sizeof(message));
        message.schema_version = read_u16(&cursor[0]);
        message.priority = (pet_message_priority_t)cursor[2];
        message.state = (pet_message_ack_state_t)cursor[3];
        message.template_id = (pet_message_template_t)cursor[4];
        message.sound = (pet_message_sound_t)cursor[5];
        message.require_ack = cursor[6] == 1u;
        message.created_at_epoch_s = read_u32(&cursor[8]);
        message.expires_at_epoch_s = read_u32(&cursor[12]);
        message.duration_ms = read_u32(&cursor[16]);

        const uint8_t *strings = &cursor[RECORD_FIXED_SIZE];
        if (!read_fixed_string(&strings[0], PET_MESSAGE_ID_MAX + 1u, message.id,
                               sizeof(message.id)) ||
            !read_fixed_string(&strings[PET_MESSAGE_ID_MAX + 1u], PET_MESSAGE_SOURCE_MAX + 1u,
                               message.source, sizeof(message.source)) ||
            !read_fixed_string(&strings[(PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u)],
                               PET_MESSAGE_DEDUPE_KEY_MAX + 1u, message.dedupe_key,
                               sizeof(message.dedupe_key)) ||
            !read_fixed_string(&strings[(PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u) +
                                        (PET_MESSAGE_DEDUPE_KEY_MAX + 1u)],
                               PET_MESSAGE_TITLE_MAX + 1u, message.title, sizeof(message.title)) ||
            !read_fixed_string(&strings[(PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u) +
                                        (PET_MESSAGE_DEDUPE_KEY_MAX + 1u) +
                                        (PET_MESSAGE_TITLE_MAX + 1u)],
                               PET_MESSAGE_BODY_MAX + 1u, message.body, sizeof(message.body))) {
            return PET_MESSAGE_STORE_ERR_RECORD;
        }

        if ((unsigned)message.priority >= (unsigned)PET_MESSAGE_PRIORITY_COUNT ||
            (unsigned)message.state >= (unsigned)PET_MESSAGE_STATE_COUNT ||
            (unsigned)message.template_id >= (unsigned)PET_MESSAGE_TEMPLATE_COUNT ||
            (unsigned)message.sound >= (unsigned)PET_MESSAGE_SOUND_COUNT ||
            cursor[6] > 1u || cursor[7] != 0) {
            return PET_MESSAGE_STORE_ERR_RECORD;
        }

        messages[i] = message;
        cursor += PET_MESSAGE_STORE_RECORD_SIZE;
    }

    *count = stored_count;
    *dedupe_next = stored_dedupe_next;
    return PET_MESSAGE_STORE_OK;
}
