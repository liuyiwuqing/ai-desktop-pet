#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "message_codec.h"
#include "message_validate.h"

#define MESSAGE_ID "11111111-2222-4333-8444-555555555555"
#define EVENT_ID "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee"
#define DEVICE_ID "dodo-01"

/* DeliveryWorker 的字段与单位；字段顺序故意不同于常见 JSON 示例。 */
static const char envelope[] =
    "{\"payload\":{\"summary\":\"你好，桌宠\",\"priority\":\"high\","
    "\"eventType\":\"comment.created\",\"title\":\"新评论\",\"source\":\"Halo_Blog\","
    "\"messageId\":\"" MESSAGE_ID "\"},\"expiresAt\":1794448800,"
    "\"deviceId\":\"" DEVICE_ID "\",\"eventId\":\"" EVENT_ID "\","
    "\"schemaVersion\":1,\"sentAt\":1794362400,\"type\":\"inbox.message\"}";

static pet_message_view_t message;

static void reject_replacement(const char *from, const char *to)
{
    char json[2048];
    const char *at = strstr(envelope, from);
    assert(at != NULL);
    int length = snprintf(json, sizeof(json), "%.*s%s%s", (int)(at - envelope), envelope,
                          to, at + strlen(from));
    assert(length > 0 && (size_t)length < sizeof(json));
    assert(pet_message_parse_nexus(json, (size_t)length, DEVICE_ID, &message) != PET_MESSAGE_OK);
}

int main(int argc, char **argv)
{
    assert(pet_message_parse_nexus(envelope, strlen(envelope), DEVICE_ID, &message) == PET_MESSAGE_OK);
    assert(strcmp(message.id, MESSAGE_ID) == 0);
    assert(strcmp(message.delivery.event_id, EVENT_ID) == 0);
    assert(strcmp(message.delivery.device_id, DEVICE_ID) == 0);
    assert(strcmp(message.body, "你好，桌宠") == 0);
    assert(message.priority == PET_MESSAGE_PRIORITY_HIGH);
    assert(message.template_id == PET_MESSAGE_TEMPLATE_COMMENT_CARD);
    assert(message.require_ack && message.duration_ms == 8000);
    assert(message.created_at_epoch_s == 1794362400 && message.expires_at_epoch_s == 1794448800);
    assert(pet_message_validate(&message, 1794362400) == PET_MESSAGE_OK);
    assert(pet_message_parse_nexus(envelope, strlen(envelope), "other-device", &message) != PET_MESSAGE_OK);

    reject_replacement("\"schemaVersion\":1", "\"schemaVersion\":2");
    reject_replacement("\"schemaVersion\":1", "\"schemaVersion\":1,\"schemaVersion\":1");
    reject_replacement("\"source\":\"Halo_Blog\"", "\"source\":\"Halo_Blog\",\"source\":\"halo\"");
    reject_replacement("inbox.message", "inbox.other");
    reject_replacement(MESSAGE_ID, "msg_legacy1");
    reject_replacement(EVENT_ID, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeeg");
    reject_replacement("\"deviceId\"", "\"unknown\"");
    reject_replacement("1794448800", "1794362400");
    reject_replacement("1794448800", "4294967296");
    reject_replacement("1794362400", "0");
    reject_replacement("1794362400", "\"1794362400\"");
    reject_replacement("Halo_Blog", "bad/source");
    reject_replacement("comment.created", "bad/event");
    reject_replacement("你好，桌宠", "\\u0000hidden");
    reject_replacement("你好，桌宠", "\\ud800");
    reject_replacement("\"priority\":\"high\",", "");

    char boundary[300];
    memset(boundary, 'a', 97);
    boundary[97] = 0;
    reject_replacement("新评论", boundary);
    memset(boundary, 'a', 181);
    boundary[181] = 0;
    reject_replacement("你好，桌宠", boundary);
    memset(boundary, 'a', 33);
    boundary[33] = 0;
    reject_replacement("Halo_Blog", boundary);

    char padded[1025];
    memset(padded, ' ', sizeof(padded));
    memcpy(padded, envelope, strlen(envelope));
    assert(pet_message_parse_nexus(padded, 1024, DEVICE_ID, &message) == PET_MESSAGE_OK);
    assert(pet_message_parse_nexus(padded, 1025, DEVICE_ID, &message) != PET_MESSAGE_OK);
    for (size_t i = 0; i < strlen(envelope); ++i)
        assert(pet_message_parse_nexus(envelope, i, DEVICE_ID, &message) != PET_MESSAGE_OK);

    assert(pet_message_parse_nexus(envelope, strlen(envelope), DEVICE_ID, &message) == PET_MESSAGE_OK);
    char ack[320];
    int length = pet_message_nexus_ack_format(ack, sizeof(ack), message.id, &message.delivery,
                                             PET_MESSAGE_STATE_READ, false);
    assert(length > 0 && (size_t)length == strlen(ack));
    assert(strcmp(ack, "{\"schemaVersion\":1,\"deviceId\":\"" DEVICE_ID "\","
                       "\"eventId\":\"" EVENT_ID "\",\"messageId\":\"" MESSAGE_ID "\","
                       "\"status\":\"read\"}") == 0);
    assert(pet_message_nexus_ack_format(ack, 10, message.id, &message.delivery,
                                         PET_MESSAGE_STATE_READ, false) == -1);
    assert(pet_message_nexus_ack_format(ack, sizeof(ack), message.id, &message.delivery,
                                         PET_MESSAGE_STATE_READ, true) > 0);
    assert(strstr(ack, "\"status\":\"rejected\",\"reason\":\"expired\"") != NULL);
    message.delivery.device_id[0] = 0;
    assert(pet_message_nexus_ack_format(ack, sizeof(ack), message.id, &message.delivery,
                                         PET_MESSAGE_STATE_READ, false) == -1);

    /* 可将真实 DeliveryWorker 生成的文件交给同一解析器验证。 */
    if (argc > 1) {
        FILE *file = fopen(argv[1], "rb");
        assert(file != NULL);
        char bytes[1025];
        size_t size = fread(bytes, 1, sizeof(bytes), file);
        assert(!ferror(file));
        fclose(file);
        assert(pet_message_parse_nexus(bytes, size, DEVICE_ID, &message) == PET_MESSAGE_OK);
        assert(pet_message_validate(&message, message.created_at_epoch_s) == PET_MESSAGE_OK);
    }
    puts("nexus codec: routing, UUIDs, bounds, strict fields and acknowledgements passed");
    return 0;
}
