#include "growth_input.h"
#include "pet/growth.h"

static bool available(const pet_growth_input_t *p, unsigned choice)
{
    if (choice >= PET_GROWTH_MENU_AI_TALK)
        return p->voice_enabled && choice <= PET_GROWTH_MENU_AI_CLEAR;
    if (choice == PET_GROWTH_MENU_RETURN) return true;
    if (choice == PET_GROWTH_MENU_NETWORK) return p->network_enabled;
    return !p->voice_only && (choice != PET_GROWTH_MENU_CLIMATE || p->climate_enabled);
}

static void move(pet_growth_input_t *p, bool backwards)
{
    const unsigned count = PET_GROWTH_MENU_AI_CLEAR + 1;
    do {
        p->selection = (p->selection + count + (backwards ? -1 : 1)) % count;
    } while (!available(p, p->selection));
}

static void open_menu(pet_growth_input_t *p)
{
    p->menu = true;
    p->selection = p->voice_enabled && (p->voice_only || p->voice_active || p->voice_ready) ?
        PET_GROWTH_MENU_AI_TALK : 0;
}

static int activate(pet_growth_input_t *p)
{
    /* PTT requires a genuinely held button; direction pushes cannot start capture. */
    if (!available(p, p->selection) || p->selection == PET_GROWTH_MENU_AI_TALK) return -1;
    p->menu = false;
    return p->selection == PET_GROWTH_MENU_ACHIEVEMENTS ||
           p->selection == PET_GROWTH_MENU_RETURN ? -1 : p->selection;
}

int pet_growth_input_navigate(pet_growth_input_t *p, pet_navigation_t direction,
                             bool blocked, uint32_t now)
{
    if (blocked) return pet_growth_input_update(p, p->raw, true, now);
    if (direction == PET_NAV_LEFT) {
        const bool cancel = p->holding || p->voice_active;
        p->holding = p->menu = p->press_voice = false;
        p->armed = !p->raw;
        return cancel ? PET_GROWTH_INPUT_AI_CANCEL : -1;
    }
    if (p->raw || p->stable || p->holding) return -1;
    if (!p->menu) {
        if (direction == PET_NAV_RIGHT) open_menu(p);
        return -1;
    }
    if (direction == PET_NAV_UP || direction == PET_NAV_DOWN) move(p, direction == PET_NAV_UP);
    else if (direction == PET_NAV_RIGHT) return activate(p);
    return -1;
}

int pet_growth_input_update(pet_growth_input_t *p, bool pressed, bool blocked, uint32_t now)
{
    if (!p->initialized || blocked) {
        const bool cancel = p->holding;
        p->initialized = true;
        p->raw = p->stable = pressed;
        p->armed = !pressed;
        p->changed_ms = now;
        p->menu = false;
        p->holding = false;
        p->press_voice = false;
        return cancel ? PET_GROWTH_INPUT_AI_CANCEL : -1;
    }
    if (pressed != p->raw) {
        p->raw = pressed;
        p->changed_ms = now;
        if (pressed) {
            p->pressed_ms = now;
            p->press_voice = p->voice_active;
        }
    }
    if (p->voice_enabled && p->menu && p->selection == PET_GROWTH_MENU_AI_TALK &&
        p->armed && p->stable && pressed && now - p->pressed_ms >= 800) {
        p->holding = true;
        p->menu = false;
        return PET_GROWTH_MENU_AI_TALK;
    }
    if (pressed == p->stable || now - p->changed_ms < 30) return -1;
    p->stable = pressed;
    if (pressed) return -1;
    if (!p->armed) { p->armed = true; return -1; }
    if (p->holding) {
        p->holding = false;
        return PET_GROWTH_INPUT_AI_RELEASE;
    }
    if (now - p->pressed_ms < 800) {
        if (p->menu) {
            move(p, false);
        } else if (p->press_voice) return PET_GROWTH_INPUT_AI_CANCEL;
        return -1;
    }
    if (!p->menu) {
        open_menu(p);
        return -1;
    }
    /* 没有观察到持续按住的 TALK 不在松开时补录，避免“按完才开麦”。 */
    if (p->voice_enabled && p->selection == PET_GROWTH_MENU_AI_TALK) return -1;
    return activate(p);
}
