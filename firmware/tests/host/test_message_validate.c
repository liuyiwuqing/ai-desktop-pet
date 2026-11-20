#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "message_fixture.h"
#include "message_validate.h"

static int s_failures;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            s_failures++;                                               \
        }                                                               \
    } while (0)

int main(void)
{
    const uint32_t now = 1794276000;
    pet_message_view_t message;
    message_fixture_init(&message, "msg_abc123", PET_MESSAGE_PRIORITY_NORMAL, now);
    CHECK(pet_message_validate(&message, now) == PET_MESSAGE_OK);

    CHECK(!pet_message_id_valid(NULL));
    CHECK(!pet_message_id_valid("msg_abc"));
    CHECK(!pet_message_id_valid("MSG_abc123"));
    CHECK(!pet_message_id_valid("msg_abc-def"));
    CHECK(pet_message_id_valid("msg_abc123"));
    CHECK(pet_message_id_valid("msg_ABCDEF"));

    CHECK(pet_message_source_valid("halo.comment"));
    CHECK(pet_message_source_valid("halo-comment"));
    CHECK(pet_message_source_valid("webhook.v2"));
    CHECK(!pet_message_source_valid(""));
    CHECK(!pet_message_source_valid("Halo.comment"));
    CHECK(!pet_message_source_valid("halo."));
    CHECK(!pet_message_source_valid(".halo"));
    CHECK(!pet_message_source_valid("halo..comment"));

    pet_message_view_t bad = message;
    bad.schema_version = PET_MESSAGE_SCHEMA_VERSION + 1;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_SCHEMA);

    bad = message;
    strcpy(bad.id, "msg_bad-id");
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_ID);

    bad = message;
    strcpy(bad.source, "Halo");
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_SOURCE);

    bad = message;
    bad.priority = PET_MESSAGE_PRIORITY_COUNT;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_PRIORITY);

    bad = message;
    memset(bad.title, 'a', sizeof(bad.title));
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_TITLE);

    bad = message;
    memset(bad.body, 'a', sizeof(bad.body));
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_BODY);

    bad = message;
    bad.created_at_epoch_s = 0;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_TIME);

    bad = message;
    bad.expires_at_epoch_s = bad.created_at_epoch_s;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_TIME);

    bad = message;
    bad.duration_ms = 4999;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_DISPLAY);

    bad = message;
    bad.duration_ms = 5000;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_OK);
    bad.duration_ms = 30000;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_OK);
    bad.duration_ms = 30001;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_DISPLAY);

    bad = message;
    bad.sound = PET_MESSAGE_SOUND_COUNT;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_DISPLAY);

    bad = message;
    bad.created_at_epoch_s = now - 100;
    bad.expires_at_epoch_s = now;
    CHECK(pet_message_validate(&bad, now) == PET_MESSAGE_ERR_EXPIRED);

    CHECK(pet_message_template_supported(PET_MESSAGE_TEMPLATE_COMMENT_CARD));
    CHECK(!pet_message_template_supported(PET_MESSAGE_TEMPLATE_COUNT));
    CHECK(pet_message_template_fallback(PET_MESSAGE_TEMPLATE_COUNT) ==
          PET_MESSAGE_TEMPLATE_TEXT_CARD);
    CHECK(pet_message_template_fallback(PET_MESSAGE_TEMPLATE_ALERT_CARD) ==
          PET_MESSAGE_TEMPLATE_ALERT_CARD);

    CHECK(pet_message_event_priority(PET_MESSAGE_PRIORITY_LOW) == PET_EVENT_PRIORITY_LOW);
    CHECK(pet_message_event_priority(PET_MESSAGE_PRIORITY_NORMAL) == PET_EVENT_PRIORITY_NORMAL);
    CHECK(pet_message_event_priority(PET_MESSAGE_PRIORITY_HIGH) == PET_EVENT_PRIORITY_HIGH);
    CHECK(pet_message_event_priority(PET_MESSAGE_PRIORITY_CRITICAL) == PET_EVENT_PRIORITY_CRITICAL);

    pet_message_notify_policy_t policy =
        pet_message_notify_policy(&message, false, true);
    CHECK(policy.display && policy.sound && !policy.alert_animation);

    message.priority = PET_MESSAGE_PRIORITY_NORMAL;
    policy = pet_message_notify_policy(&message, true, true);
    CHECK(!policy.display && !policy.sound && !policy.alert_animation);

    message.priority = PET_MESSAGE_PRIORITY_LOW;
    policy = pet_message_notify_policy(&message, true, true);
    CHECK(!policy.display && !policy.sound);

    message.priority = PET_MESSAGE_PRIORITY_HIGH;
    policy = pet_message_notify_policy(&message, true, true);
    CHECK(policy.display && !policy.sound && !policy.alert_animation);

    message.priority = PET_MESSAGE_PRIORITY_CRITICAL;
    policy = pet_message_notify_policy(&message, true, true);
    CHECK(policy.display && policy.sound && policy.alert_animation);

    policy = pet_message_notify_policy(&message, true, false);
    CHECK(!policy.display && !policy.sound && !policy.alert_animation);

    message.sound = PET_MESSAGE_SOUND_NONE;
    policy = pet_message_notify_policy(&message, false, true);
    CHECK(policy.display && !policy.sound && policy.alert_animation);

    CHECK(pet_message_display_duration_ms(15000, 8000) == 8000);
    CHECK(pet_message_display_duration_ms(5000, 8000) == 5000);
    CHECK(pet_message_display_duration_ms(8000, 8000) == 8000);

    CHECK(pet_message_ack_transition_allowed(PET_MESSAGE_STATE_RECEIVED,
                                             PET_MESSAGE_STATE_DISPLAYED));
    CHECK(pet_message_ack_transition_allowed(PET_MESSAGE_STATE_RECEIVED,
                                             PET_MESSAGE_STATE_READ));
    CHECK(pet_message_ack_transition_allowed(PET_MESSAGE_STATE_DISPLAYED,
                                             PET_MESSAGE_STATE_READ));
    CHECK(!pet_message_ack_transition_allowed(PET_MESSAGE_STATE_DISPLAYED,
                                              PET_MESSAGE_STATE_RECEIVED));
    CHECK(!pet_message_ack_transition_allowed(PET_MESSAGE_STATE_READ,
                                              PET_MESSAGE_STATE_DISPLAYED));
    CHECK(pet_message_ack_transition_allowed(PET_MESSAGE_STATE_READ, PET_MESSAGE_STATE_READ));
    CHECK(!pet_message_ack_transition_allowed(PET_MESSAGE_STATE_COUNT,
                                              PET_MESSAGE_STATE_READ));

    pet_message_event_t event;
    memset(&event, 0, sizeof(event));
    strcpy(event.id, message.id);
    event.state = PET_MESSAGE_STATE_RECEIVED;
    event.queue_depth = 3;
    CHECK(sizeof(event) <= PET_EVENT_PAYLOAD_MAX);
    CHECK(strcmp(event.id, "msg_abc123") == 0);

    if (s_failures == 0) {
        printf("host tests passed: message validate\n");
    }
    return s_failures == 0 ? 0 : 1;
}
