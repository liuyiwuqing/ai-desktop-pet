#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "pet/presentation/message_presenter.h"
#include "message_fixture.h"

static void displayed_requires_a_completed_frame(void)
{
    pet_message_presenter_t presenter;
    pet_message_presenter_init(&presenter);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_first1", PET_MESSAGE_PRIORITY_NORMAL, 100);
    const pet_message_screen_action_t action =
        pet_message_presenter_tick(&presenter, &message, 1, 1000, 101, false, true);
    assert(action.kind == PET_MESSAGE_SCREEN_SHOW);
    assert(strcmp(action.id, "msg_first1") == 0);
    assert(!pet_message_presenter_rendered(&presenter, action.generation + 1, true, 1010));
    assert(pet_message_presenter_rendered(&presenter, action.generation, true, 1010));
    assert(!pet_message_presenter_rendered(&presenter, action.generation, true, 1020));
}

static void failed_frame_falls_back_without_ack_and_retries(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_fail01", PET_MESSAGE_PRIORITY_NORMAL, 100);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, 1000, 101, false, true);
    assert(!pet_message_presenter_rendered(&p, a.generation, false, 1010));
    a = pet_message_presenter_tick(&p, &message, 1, 1020, 101, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && a.plain_text);
    assert(!pet_message_presenter_rendered(&p, a.generation, false, 1030));
    a = pet_message_presenter_tick(&p, &message, 1, 1040, 101, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_HIDE);
    assert(pet_message_presenter_tick(&p, &message, 1, 1500, 101, false, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    a = pet_message_presenter_tick(&p, &message, 1, 2030, 102, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 2040));
}

static void dnd_expiry_and_critical_preemption(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t messages[2];
    message_fixture_init(&messages[1], "msg_normal", PET_MESSAGE_PRIORITY_NORMAL, 100);
    assert(pet_message_presenter_tick(&p, &messages[1], 1, 1000, 101, true, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &messages[1], 1, 1000, 101, false, true);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1010));
    const uint32_t old_generation = a.generation;
    message_fixture_init(&messages[0], "msg_critical", PET_MESSAGE_PRIORITY_CRITICAL, 101);
    a = pet_message_presenter_tick(&p, messages, 2, 1020, 101, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && strcmp(a.id, "msg_critical") == 0);
    assert(!pet_message_presenter_rendered(&p, old_generation, true, 1030));
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1030));
    messages[0].expires_at_epoch_s = 102;
    assert(pet_message_presenter_tick(&p, messages, 2, 2000, 102, false, true).kind ==
           PET_MESSAGE_SCREEN_HIDE);
    a = pet_message_presenter_tick(&p, messages, 2, 2010, 102, false, true);
    assert(strcmp(a.id, "msg_normal") == 0);
}

static void timeout_keeps_unread_without_endless_popups(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_timeout", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.duration_ms = 5000;
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, 1000, 101, false, true);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1010));
    assert(pet_message_presenter_tick(&p, &message, 1, 6009, 106, false, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    assert(pet_message_presenter_tick(&p, &message, 1, 6010, 106, false, true).kind ==
           PET_MESSAGE_SCREEN_HIDE);
    assert(pet_message_presenter_tick(&p, &message, 1, 6020, 106, false, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    assert(message.state == PET_MESSAGE_STATE_RECEIVED);
}

static pet_message_screen_action_t click(pet_message_presenter_t *p, uint32_t now)
{
    pet_message_presenter_button(p, true, now);
    pet_message_presenter_button(p, true, now + 30);
    pet_message_presenter_button(p, false, now + 40);
    return pet_message_presenter_button(p, false, now + 70);
}

static void read_is_bound_to_visible_frame_and_short_press(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t messages[2];
    message_fixture_init(&messages[1], "msg_old001", PET_MESSAGE_PRIORITY_NORMAL, 100);
    pet_message_presenter_button(&p, false, 900);
    pet_message_screen_action_t frame =
        pet_message_presenter_tick(&p, &messages[1], 1, 1000, 101, false, true);
    assert(click(&p, 1001).kind == PET_MESSAGE_SCREEN_NONE);
    assert(pet_message_presenter_rendered(&p, frame.generation, true, 1075));
    pet_message_screen_action_t read = click(&p, 1200);
    assert(read.kind == PET_MESSAGE_SCREEN_READ && strcmp(read.id, "msg_old001") == 0);

    pet_message_presenter_button(&p, true, 1400);
    pet_message_presenter_button(&p, true, 1435);
    message_fixture_init(&messages[0], "msg_new001", PET_MESSAGE_PRIORITY_CRITICAL, 101);
    frame = pet_message_presenter_tick(&p, messages, 2, 1450, 101, false, true);
    assert(pet_message_presenter_rendered(&p, frame.generation, true, 1460));
    pet_message_presenter_button(&p, false, 1480);
    assert(pet_message_presenter_button(&p, false, 1515).kind == PET_MESSAGE_SCREEN_NONE);
    assert(!pet_message_presenter_read_complete(&p, read.generation));
    read = click(&p, 1600);
    assert(read.kind == PET_MESSAGE_SCREEN_READ && strcmp(read.id, "msg_new001") == 0);
    assert(pet_message_presenter_read_complete(&p, read.generation));
    assert(!pet_message_presenter_read_complete(&p, read.generation));
}

static void boot_held_at_startup_and_long_press_are_ignored(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_boot01", PET_MESSAGE_PRIORITY_NORMAL, 100);
    pet_message_presenter_button(&p, true, 900);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, 1000, 101, false, true);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1010));
    pet_message_presenter_button(&p, false, 1100);
    assert(pet_message_presenter_button(&p, false, 1135).kind == PET_MESSAGE_SCREEN_NONE);
    pet_message_presenter_button(&p, true, 1200);
    pet_message_presenter_button(&p, true, 1235);
    pet_message_presenter_button(&p, false, 2200);
    assert(pet_message_presenter_button(&p, false, 2235).kind == PET_MESSAGE_SCREEN_NONE);
}

static void idle_click_reopens_timed_out_message_even_in_dnd(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_presenter_button(&p, false, 900);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_browse", PET_MESSAGE_PRIORITY_NORMAL, 100);
    assert(pet_message_presenter_tick(&p, &message, 1, 1000, 101, true, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    assert(click(&p, 1100).kind == PET_MESSAGE_SCREEN_NONE);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, 1300, 101, true, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1310));
    assert(pet_message_presenter_tick(&p, &message, 1, 1320, 101, true, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
}

static void manual_browsing_keeps_long_message_visible_until_confirmed(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_presenter_button(&p, false, 900);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_manual", PET_MESSAGE_PRIORITY_NORMAL, 100);
    message.duration_ms = 5000;
    /* 手动打开时绕过弹窗时限，长正文才有机会滚动到底。 */
    click(&p, 1000);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, 1100, 101, true, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1110));
    assert(pet_message_presenter_tick(&p, &message, 1, 61110, 161, true, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    a = click(&p, 61200);
    assert(a.kind == PET_MESSAGE_SCREEN_READ && strcmp(a.id, "msg_manual") == 0);
    assert(pet_message_presenter_read_complete(&p, a.generation));
}

static void high_waits_for_current_message_and_display_loss_cancels_read(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_presenter_button(&p, false, 900);
    pet_message_view_t messages[2];
    message_fixture_init(&messages[1], "msg_normal", PET_MESSAGE_PRIORITY_NORMAL, 100);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &messages[1], 1, 1000, 101, false, true);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1010));
    message_fixture_init(&messages[0], "msg_high01", PET_MESSAGE_PRIORITY_HIGH, 101);
    assert(pet_message_presenter_tick(&p, messages, 2, 1020, 101, false, true).kind ==
           PET_MESSAGE_SCREEN_NONE);
    pet_message_presenter_display_lost(&p);
    assert(click(&p, 1100).kind == PET_MESSAGE_SCREEN_NONE);
    a = pet_message_presenter_tick(&p, messages, 2, 1200, 101, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && a.plain_text);
    assert(strcmp(a.id, "msg_normal") == 0);
}

static void missing_queue_entry_and_render_timeout_never_read(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_presenter_button(&p, false, 900);
    pet_message_view_t message;
    message_fixture_init(&message, "msg_expire", PET_MESSAGE_PRIORITY_NORMAL, 100);
    pet_message_screen_action_t a =
        pet_message_presenter_tick(&p, &message, 1, UINT32_MAX - 50, 0, false, true);
    /* 单调时钟溢出后，超过 100 ms 的晚到成功仍不能发送 displayed。 */
    assert(!pet_message_presenter_rendered(&p, a.generation, true, 60));
    a = pet_message_presenter_tick(&p, &message, 1, 61, 0, false, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && a.plain_text);
    assert(pet_message_presenter_tick(&p, NULL, 0, 70, 101, false, true).kind ==
           PET_MESSAGE_SCREEN_HIDE);
    assert(!pet_message_presenter_rendered(&p, a.generation, true, 80));
    assert(click(&p, 90).kind == PET_MESSAGE_SCREEN_NONE);
}

static void voice_defers_popups_but_critical_preempts(void)
{
    pet_message_presenter_t p;
    pet_message_presenter_init(&p);
    pet_message_view_t messages[2];
    message_fixture_init(&messages[1], "msg_deferred", PET_MESSAGE_PRIORITY_HIGH, 100);
    assert(pet_message_presenter_tick_focused(&p, &messages[1], 1, 1000, 101, false, true,
                                             true).kind == PET_MESSAGE_SCREEN_NONE);
    assert(p.id[0] == '\0');
    message_fixture_init(&messages[0], "msg_interrupt", PET_MESSAGE_PRIORITY_CRITICAL, 100);
    pet_message_screen_action_t a = pet_message_presenter_tick_focused(
        &p, messages, 2, 1010, 101, true, true, true);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && strcmp(a.id, "msg_interrupt") == 0);
    assert(pet_message_presenter_rendered(&p, a.generation, true, 1020));
    assert(pet_message_presenter_read_complete(&p, a.generation));
    messages[0].state = PET_MESSAGE_STATE_READ;
    a = pet_message_presenter_tick_focused(&p, messages, 2, 1030, 101, false, true, false);
    assert(a.kind == PET_MESSAGE_SCREEN_SHOW && strcmp(a.id, "msg_deferred") == 0);
}

int main(void)
{
    displayed_requires_a_completed_frame();
    failed_frame_falls_back_without_ack_and_retries();
    dnd_expiry_and_critical_preemption();
    timeout_keeps_unread_without_endless_popups();
    read_is_bound_to_visible_frame_and_short_press();
    boot_held_at_startup_and_long_press_are_ignored();
    idle_click_reopens_timed_out_message_even_in_dnd();
    manual_browsing_keeps_long_message_visible_until_confirmed();
    high_waits_for_current_message_and_display_loss_cancels_read();
    missing_queue_entry_and_render_timeout_never_read();
    voice_defers_popups_but_critical_preempts();
    puts("message presenter: PASS");
    return 0;
}
