#include "pet/presentation/message_presenter.h"

#include <string.h>

#define RENDER_TIMEOUT_MS 100U
#define RENDER_RETRY_MS 1000U

void pet_message_presenter_init(pet_message_presenter_t *p) { memset(p, 0, sizeof(*p)); }

static const pet_message_view_t *find(const pet_message_view_t *messages, size_t count,
                                      const char *id)
{
    for (size_t i = 0; i < count; ++i) {
        if (strcmp(messages[i].id, id) == 0)
            return &messages[i];
    }
    return NULL;
}

static void next_generation(pet_message_presenter_t *p)
{
    if (++p->generation == 0)
        ++p->generation;
}

static void clear_active(pet_message_presenter_t *p)
{
    next_generation(p);
    p->id[0] = '\0';
    p->rendering = false;
    p->visible = false;
    p->retry_pending = false;
    p->retry_wait = false;
    p->needs_hide = false;
    p->plain_text = false;
    p->manual = false;
}

static bool was_seen(const pet_message_presenter_t *p, const char *id)
{
    for (size_t i = 0; i < p->seen_count; ++i) {
        if (strcmp(p->seen[i], id) == 0)
            return true;
    }
    return false;
}

static bool eligible(const pet_message_view_t *message, uint32_t epoch, bool dnd,
                     bool allow_critical)
{
    return message->state != PET_MESSAGE_STATE_READ &&
           (epoch == 0 || !pet_message_expired(message, epoch)) &&
           pet_message_notify_policy(message, dnd, allow_critical).display;
}

static pet_message_screen_action_t show(pet_message_presenter_t *p,
                                        const pet_message_view_t *message, size_t index,
                                        uint32_t now_ms, bool plain_text)
{
    pet_message_screen_action_t action = {.kind = PET_MESSAGE_SCREEN_SHOW, .index = index};
    memcpy(p->id, message->id, sizeof(p->id));
    next_generation(p);
    p->rendering = true;
    p->visible = false;
    p->retry_pending = false;
    p->retry_wait = false;
    p->needs_hide = false;
    p->plain_text = plain_text;
    p->render_started_ms = now_ms;
    p->duration_ms = message->duration_ms < 5000 ? 5000 : message->duration_ms;
    if (p->duration_ms > 30000)
        p->duration_ms = 30000;
    memcpy(action.id, p->id, sizeof(action.id));
    action.generation = p->generation;
    action.plain_text = plain_text;
    return action;
}

pet_message_screen_action_t pet_message_presenter_tick_focused(pet_message_presenter_t *p,
                                                       const pet_message_view_t *messages,
                                                       size_t count, uint32_t now_ms,
                                                       uint32_t now_epoch_s, bool dnd,
                                                       bool allow_critical, bool voice_focused)
{
    pet_message_screen_action_t action = {0};
    if (messages == NULL)
        count = 0;
    if (count > PET_MESSAGE_QUEUE_CAPACITY)
        count = PET_MESSAGE_QUEUE_CAPACITY;

    /* 只记住当前队列内已结束的弹窗，避免新消息挤掉旧 ID 后反复弹出。 */
    for (size_t i = 0; i < p->seen_count;) {
        if (find(messages, count, p->seen[i]) == NULL) {
            memmove(p->seen[i], p->seen[i + 1], (p->seen_count - i - 1) * sizeof(p->seen[0]));
            --p->seen_count;
        } else {
            ++i;
        }
    }

    const pet_message_view_t *current = find(messages, count, p->id);
    if (p->id[0] != '\0' &&
        (current == NULL ||
         !eligible(current, now_epoch_s, p->manual ? false : dnd, allow_critical))) {
        clear_active(p);
        action.kind = PET_MESSAGE_SCREEN_HIDE;
        return action;
    }
    /* 自动弹窗限时；用户主动打开的消息保留，避免长正文永远滚不到末尾。 */
    if (current != NULL && p->visible && !p->manual &&
        now_ms - p->visible_since_ms >= p->duration_ms) {
        if (p->seen_count < PET_MESSAGE_QUEUE_CAPACITY && !was_seen(p, p->id)) {
            memcpy(p->seen[p->seen_count++], p->id, sizeof(p->id));
        }
        clear_active(p);
        action.kind = PET_MESSAGE_SCREEN_HIDE;
        return action;
    }
    const bool browse = p->browse_requested && current == NULL;
    p->browse_requested = false;
    if (browse)
        p->seen_count = 0;
    for (size_t i = 0; i < count; ++i) {
        const pet_message_view_t *candidate = &messages[i];
        if (voice_focused && candidate->priority != PET_MESSAGE_PRIORITY_CRITICAL) continue;
        if (!eligible(candidate, now_epoch_s, browse ? false : dnd, allow_critical) ||
            was_seen(p, candidate->id))
            continue;
        if (current == NULL || (candidate->priority == PET_MESSAGE_PRIORITY_CRITICAL &&
                                current->priority != PET_MESSAGE_PRIORITY_CRITICAL)) {
            p->manual = browse;
            return show(p, candidate, i, now_ms, false);
        }
    }
    if (current == NULL)
        return action;
    if (p->rendering && now_ms - p->render_started_ms >= RENDER_TIMEOUT_MS) {
        pet_message_presenter_rendered(p, p->generation, false, now_ms);
    }
    if (p->needs_hide) {
        p->needs_hide = false;
        action.kind = PET_MESSAGE_SCREEN_HIDE;
        return action;
    }
    if (p->retry_pending || (p->retry_wait && now_ms - p->failed_at_ms >= RENDER_RETRY_MS)) {
        return show(p, current, (size_t)(current - messages), now_ms, p->retry_pending);
    }
    return action;
}

pet_message_screen_action_t pet_message_presenter_tick(pet_message_presenter_t *p,
    const pet_message_view_t *messages, size_t count, uint32_t now_ms, uint32_t now_epoch_s,
    bool dnd, bool allow_critical)
{
    return pet_message_presenter_tick_focused(p, messages, count, now_ms, now_epoch_s,
                                             dnd, allow_critical, false);
}

bool pet_message_presenter_rendered(pet_message_presenter_t *p, uint32_t generation, bool success,
                                    uint32_t now_ms)
{
    if (generation != p->generation || !p->rendering)
        return false;
    p->rendering = false;
    if (!success || now_ms - p->render_started_ms > RENDER_TIMEOUT_MS) {
        p->visible = false;
        if (!p->plain_text) {
            p->retry_pending = true;
        } else {
            p->retry_wait = true;
            p->failed_at_ms = now_ms;
            p->needs_hide = true;
        }
        return false;
    }
    p->visible = true;
    p->visible_since_ms = now_ms;
    return true;
}

pet_message_screen_action_t pet_message_presenter_button(pet_message_presenter_t *p, bool pressed,
                                                         uint32_t now_ms)
{
    pet_message_screen_action_t action = {0};
    if (!p->button_initialized) {
        p->button_initialized = true;
        p->button_raw = pressed;
        p->button_stable = pressed;
        p->button_armed = !pressed;
        p->button_changed_ms = now_ms;
        return action;
    }
    if (pressed != p->button_raw) {
        p->button_raw = pressed;
        p->button_changed_ms = now_ms;
        if (pressed && !p->button_stable) {
            p->button_generation = p->visible ? p->generation : 0;
            p->button_target_idle = p->id[0] == '\0';
            p->button_pressed_ms = now_ms;
        }
    }
    if (pressed == p->button_stable || now_ms - p->button_changed_ms < 30)
        return action;
    p->button_stable = pressed;
    if (pressed)
        return action;
    if (!p->button_armed) {
        p->button_armed = true;
        return action;
    }
    if (now_ms - p->button_pressed_ms >= 800)
        return action;
    if (p->button_generation != 0 && p->button_generation == p->generation && p->visible) {
        action.kind = PET_MESSAGE_SCREEN_READ;
        action.generation = p->generation;
        memcpy(action.id, p->id, sizeof(action.id));
    } else if (p->button_target_idle && p->id[0] == '\0') {
        p->browse_requested = true;
    }
    return action;
}

bool pet_message_presenter_read_complete(pet_message_presenter_t *p, uint32_t generation)
{
    if (!p->visible || p->generation != generation)
        return false;
    clear_active(p);
    return true;
}

void pet_message_presenter_display_lost(pet_message_presenter_t *p)
{
    p->visible = false;
    p->retry_pending = p->id[0] != '\0';
}
