#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "pet/event.h"

/* 消息中心跨模块契约：纯 C，不依赖 ESP-IDF，可在主机测试中直接编译。 */

#define PET_MESSAGE_SCHEMA_VERSION 1
#define PET_MESSAGE_ID_MAX 44       /* msg_ + 最多 40 个字母数字 */
#define PET_MESSAGE_SOURCE_MAX 64
#define PET_MESSAGE_TITLE_MAX 120
#define PET_MESSAGE_BODY_MAX 2048
#define PET_MESSAGE_DEDUPE_KEY_MAX 128
#define PET_MESSAGE_QUEUE_CAPACITY 8
#define PET_MESSAGE_DEDUPE_SLOTS 16

typedef enum {
    PET_MESSAGE_PRIORITY_LOW = 0,
    PET_MESSAGE_PRIORITY_NORMAL,
    PET_MESSAGE_PRIORITY_HIGH,
    PET_MESSAGE_PRIORITY_CRITICAL,
    PET_MESSAGE_PRIORITY_COUNT,
} pet_message_priority_t;

typedef enum {
    PET_MESSAGE_STATE_RECEIVED = 0,
    PET_MESSAGE_STATE_DISPLAYED,
    PET_MESSAGE_STATE_READ,
    PET_MESSAGE_STATE_COUNT,
} pet_message_ack_state_t;

typedef enum {
    PET_MESSAGE_TEMPLATE_TEXT_CARD = 0,
    PET_MESSAGE_TEMPLATE_COMMENT_CARD,
    PET_MESSAGE_TEMPLATE_ALERT_CARD,
    PET_MESSAGE_TEMPLATE_GROWTH_CARD,
    PET_MESSAGE_TEMPLATE_SYSTEM_CARD,
    PET_MESSAGE_TEMPLATE_COUNT,
} pet_message_template_t;

typedef enum {
    PET_MESSAGE_SOUND_NONE = 0,
    PET_MESSAGE_SOUND_CHIME,
    PET_MESSAGE_SOUND_ALERT,
    PET_MESSAGE_SOUND_COUNT,
} pet_message_sound_t;

typedef enum {
    PET_MESSAGE_OK = 0,
    PET_MESSAGE_ERR_ARG,
    PET_MESSAGE_ERR_SCHEMA,
    PET_MESSAGE_ERR_ID,
    PET_MESSAGE_ERR_SOURCE,
    PET_MESSAGE_ERR_PRIORITY,
    PET_MESSAGE_ERR_TITLE,
    PET_MESSAGE_ERR_BODY,
    PET_MESSAGE_ERR_TIME,
    PET_MESSAGE_ERR_DISPLAY,
    PET_MESSAGE_ERR_EXPIRED,
    PET_MESSAGE_ERR_DUPLICATE,
    PET_MESSAGE_ERR_FULL,
    PET_MESSAGE_ERR_STATE,
} pet_message_error_t;

/* 队列与快照共用的消息视图；正文为设备侧唯一副本，UI 只读快照。 */
typedef struct {
    uint16_t schema_version;
    pet_message_priority_t priority;
    pet_message_ack_state_t state;
    pet_message_template_t template_id;
    pet_message_sound_t sound;
    bool require_ack;
    uint32_t created_at_epoch_s;
    uint32_t expires_at_epoch_s; /* 0 表示不过期 */
    uint32_t duration_ms;
    char id[PET_MESSAGE_ID_MAX + 1];
    char source[PET_MESSAGE_SOURCE_MAX + 1];
    char dedupe_key[PET_MESSAGE_DEDUPE_KEY_MAX + 1];
    char title[PET_MESSAGE_TITLE_MAX + 1];
    char body[PET_MESSAGE_BODY_MAX + 1];
} pet_message_view_t;

/* 事件只带 ID 与状态，正文通过消息中心快照读取，避免 64 B payload 截断。 */
typedef struct {
    char id[PET_MESSAGE_ID_MAX + 1];
    pet_message_ack_state_t state;
    uint8_t queue_depth;
    uint16_t reserved;
} pet_message_event_t;

typedef struct {
    bool display;
    bool sound;
    bool alert_animation;
} pet_message_notify_policy_t;

_Static_assert(sizeof(pet_message_event_t) <= PET_EVENT_PAYLOAD_MAX,
               "message event must fit in pet_event_t payload");

bool pet_message_id_valid(const char *id);
bool pet_message_source_valid(const char *source);
bool pet_message_template_supported(pet_message_template_t template_id);
pet_message_template_t pet_message_template_fallback(pet_message_template_t template_id);
pet_event_priority_t pet_message_event_priority(pet_message_priority_t priority);
bool pet_message_expired(const pet_message_view_t *message, uint32_t now_epoch_s);
pet_message_notify_policy_t pet_message_notify_policy(const pet_message_view_t *message,
                                                      bool dnd_active, bool allow_critical);
bool pet_message_ack_transition_allowed(pet_message_ack_state_t from, pet_message_ack_state_t to);
