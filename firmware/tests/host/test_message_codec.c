#include <stdio.h>
#include <string.h>

#include "message_codec.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

static const char *const k_valid =
    "{"
    "\"schemaVersion\":1,"
    "\"id\":\"msg_01JABCDEF\","
    "\"source\":\"halo.comment\","
    "\"type\":\"comment\","
    "\"priority\":\"high\","
    "\"title\":\"新评论\","
    "\"body\":\"这篇文章很有帮助\","
    "\"payload\":{\"postTitle\":\"ESP32 入门\",\"tags\":[\"a\",2.5,-3e2,true,null]},"
    "\"createdAt\":\"2026-11-10T10:00:00+08:00\","
    "\"expiresAt\":\"2026-11-11T10:00:00+08:00\","
    "\"dedupeKey\":\"halo:comment:123456\","
    "\"display\":{\"template\":\"comment_card\",\"durationMs\":8000,"
    "\"sound\":\"chime\",\"icon\":\"comment\"},"
    "\"requireAck\":true"
    "}";

static const char *const k_minimal =
    "{"
    "\"schemaVersion\":1,"
    "\"id\":\"msg_abc123\","
    "\"source\":\"webhook\","
    "\"type\":\"text\","
    "\"priority\":\"low\","
    "\"title\":\"\","
    "\"body\":\"\","
    "\"payload\":{},"
    "\"createdAt\":\"2026-11-10T02:00:00Z\","
    "\"expiresAt\":\"2026-11-10T03:00:00Z\","
    "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
    "\"requireAck\":false"
    "}";

static void expect_error(const char *json, pet_message_error_t expected)
{
    pet_message_view_t view;
    const pet_message_error_t err = pet_message_parse(json, strlen(json), &view);
    CHECK(err == expected);
}

static void test_valid(void)
{
    pet_message_view_t view;
    CHECK(pet_message_parse(k_valid, strlen(k_valid), &view) == PET_MESSAGE_OK);
    CHECK(view.schema_version == PET_MESSAGE_SCHEMA_VERSION);
    CHECK(strcmp(view.id, "msg_01JABCDEF") == 0);
    CHECK(strcmp(view.source, "halo.comment") == 0);
    CHECK(view.priority == PET_MESSAGE_PRIORITY_HIGH);
    CHECK(view.template_id == PET_MESSAGE_TEMPLATE_COMMENT_CARD);
    CHECK(view.sound == PET_MESSAGE_SOUND_CHIME);
    CHECK(view.duration_ms == 8000);
    CHECK(view.require_ack);
    CHECK(view.state == PET_MESSAGE_STATE_RECEIVED);
    CHECK(view.created_at_epoch_s == 1794276000U);
    CHECK(view.expires_at_epoch_s == 1794362400U);
    CHECK(strcmp(view.dedupe_key, "halo:comment:123456") == 0);
    CHECK(strcmp(view.title, "新评论") == 0);
    CHECK(strcmp(view.body, "这篇文章很有帮助") == 0);

    CHECK(pet_message_parse(k_minimal, strlen(k_minimal), &view) == PET_MESSAGE_OK);
    CHECK(view.priority == PET_MESSAGE_PRIORITY_LOW);
    CHECK(view.template_id == PET_MESSAGE_TEMPLATE_TEXT_CARD);
    CHECK(view.sound == PET_MESSAGE_SOUND_NONE);
    CHECK(!view.require_ack);
    CHECK(view.title[0] == '\0');
    CHECK(view.dedupe_key[0] == '\0');
}

static void test_escapes(void)
{
    static const char *const json =
        "{"
        "\"schemaVersion\":1,"
        "\"id\":\"msg_abc123\","
        "\"source\":\"webhook\","
        "\"type\":\"text\","
        "\"priority\":\"normal\","
        "\"title\":\"a\\\"b\\\\c\\/d\","
        "\"body\":\"\\u4f60\\u597d\\ud83d\\ude00\\nend\","
        "\"payload\":{},"
        "\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":6000,\"sound\":\"none\"},"
        "\"requireAck\":true"
        "}";

    pet_message_view_t view;
    CHECK(pet_message_parse(json, strlen(json), &view) == PET_MESSAGE_OK);
    CHECK(strcmp(view.title, "a\"b\\c/d") == 0);
    CHECK(strcmp(view.body, "你好😀\nend") == 0);
}

static void test_invalid(void)
{
    expect_error("[]", PET_MESSAGE_ERR_SCHEMA);
    expect_error("{\"schemaVersion\":1}", PET_MESSAGE_ERR_SCHEMA);
    expect_error("", PET_MESSAGE_ERR_ARG);

    /* schemaVersion 不匹配 */
    expect_error(
        "{\"schemaVersion\":2,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_SCHEMA);

    /* 未知字段（schema 为 additionalProperties:false） */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false,\"extra\":1}",
        PET_MESSAGE_ERR_SCHEMA);

    /* display 未知字段 */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\","
        "\"color\":\"red\"},\"requireAck\":false}",
        PET_MESSAGE_ERR_DISPLAY);

    /* 缺失必填字段 requireAck */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"}}",
        PET_MESSAGE_ERR_SCHEMA);

    /* 重复字段 */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"id\":\"msg_abc123\","
        "\"source\":\"webhook\",\"type\":\"text\",\"priority\":\"low\",\"title\":\"\","
        "\"body\":\"\",\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_ID);

    /* 非法枚举与取值 */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"urgent\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_PRIORITY);

    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":4999,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_DISPLAY);

    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"banner\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_DISPLAY);

    /* 时间格式与时区 */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false}",
        PET_MESSAGE_ERR_TIME);

    /* 结尾多余内容 */
    expect_error(
        "{\"schemaVersion\":1,\"id\":\"msg_abc123\",\"source\":\"webhook\","
        "\"type\":\"text\",\"priority\":\"low\",\"title\":\"\",\"body\":\"\","
        "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
        "\"expiresAt\":\"2026-11-10T03:00:00Z\","
        "\"display\":{\"template\":\"text_card\",\"durationMs\":5000,\"sound\":\"none\"},"
        "\"requireAck\":false} x",
        PET_MESSAGE_ERR_SCHEMA);
}

static void test_limits(void)
{
    static char json[4200];
    static char body[2200];

    memset(body, 'a', 2049);
    body[2049] = '\0';

    const int written = snprintf(json, sizeof(json),
                                 "{\"schemaVersion\":1,\"id\":\"msg_abc123\","
                                 "\"source\":\"webhook\",\"type\":\"text\","
                                 "\"priority\":\"low\",\"title\":\"\",\"body\":\"%s\","
                                 "\"payload\":{},\"createdAt\":\"2026-11-10T02:00:00Z\","
                                 "\"expiresAt\":\"2026-11-10T03:00:00Z\","
                                 "\"display\":{\"template\":\"text_card\","
                                 "\"durationMs\":5000,\"sound\":\"none\"},"
                                 "\"requireAck\":false}",
                                 body);
    CHECK(written > 0 && (size_t)written < sizeof(json));
    expect_error(json, PET_MESSAGE_ERR_BODY);

    char too_big[PET_MESSAGE_JSON_MAX + 1];
    memset(too_big, ' ', sizeof(too_big));
    pet_message_view_t view;
    CHECK(pet_message_parse(too_big, sizeof(too_big), &view) == PET_MESSAGE_ERR_ARG);
    CHECK(pet_message_parse(k_valid, 0, &view) == PET_MESSAGE_ERR_ARG);
    CHECK(pet_message_parse(NULL, 10, &view) == PET_MESSAGE_ERR_ARG);
}

static void test_ack_format(void)
{
    char ack[160];
    int length = pet_message_ack_format(ack, sizeof(ack), "msg_01JABCDEF",
                                        PET_MESSAGE_STATE_RECEIVED, 1794276000U, 3);
    CHECK(length > 0);
    CHECK((size_t)length == strlen(ack));
    CHECK(strcmp(ack,
                 "{\"schemaVersion\":1,\"messageId\":\"msg_01JABCDEF\","
                 "\"state\":\"received\",\"at\":\"2026-11-10T02:00:00Z\","
                 "\"queueDepth\":3}") == 0);

    CHECK(pet_message_ack_format(ack, sizeof(ack), "msg_01JABCDEF",
                                 PET_MESSAGE_STATE_DISPLAYED, 1794276000U, 0) > 0);
    CHECK(strstr(ack, "\"state\":\"displayed\"") != NULL);
    CHECK(pet_message_ack_format(ack, sizeof(ack), "msg_01JABCDEF",
                                 PET_MESSAGE_STATE_READ, 1794276000U, 0) > 0);
    CHECK(strstr(ack, "\"state\":\"read\"") != NULL);

    CHECK(pet_message_ack_format(ack, sizeof(ack), "msg_01JABCDEF",
                                 PET_MESSAGE_STATE_RECEIVED, 1794276000U, 33) == -1);
    CHECK(pet_message_ack_format(ack, sizeof(ack), "msg_01JABCDEF",
                                 PET_MESSAGE_STATE_COUNT, 1794276000U, 0) == -1);
    CHECK(pet_message_ack_format(ack, 32, "msg_01JABCDEF", PET_MESSAGE_STATE_RECEIVED,
                                 1794276000U, 0) == -1);
}

int main(void)
{
    test_valid();
    test_escapes();
    test_invalid();
    test_limits();
    test_ack_format();

    if (s_failures == 0) {
        printf("host tests passed: message codec\n");
    }
    return s_failures == 0 ? 0 : 1;
}
