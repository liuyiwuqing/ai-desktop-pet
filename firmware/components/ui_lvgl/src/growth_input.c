#include "growth_input.h"
#include "pet/growth.h"
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
            if (!p->voice_enabled) p->selection = (p->selection + 1) % PET_GROWTH_MENU_COUNT;
            else if (p->selection == PET_GROWTH_MENU_AI_CLEAR)
                p->selection = p->voice_only ? PET_GROWTH_MENU_RETURN : 0;
            else if (p->selection == PET_GROWTH_MENU_RETURN)
                p->selection = PET_GROWTH_MENU_AI_TALK;
            else ++p->selection;
        } else if (p->press_voice) return PET_GROWTH_INPUT_AI_CANCEL;
        return -1;
    }
    if (!p->menu) {
        p->menu = true;
        p->selection = p->voice_only || p->voice_active || (p->voice_enabled && p->voice_ready) ?
            PET_GROWTH_MENU_AI_TALK : 0;
        return -1;
    }
    /* 没有观察到持续按住的 TALK 不在松开时补录，避免“按完才开麦”。 */
    if (p->voice_enabled && p->selection == PET_GROWTH_MENU_AI_TALK) return -1;
    p->menu = false;
    return p->selection == PET_GROWTH_MENU_CLIMATE || (p->selection>=PET_GROWTH_MENU_FOCUS && p->selection<=PET_GROWTH_MENU_FOCUS_CANCEL) || p->selection < PET_GROWTH_ACTION_COUNT ||
        (p->voice_enabled && (p->selection == PET_GROWTH_MENU_AI_MUTE ||
                              p->selection == PET_GROWTH_MENU_AI_DND || p->selection == PET_GROWTH_MENU_AI_CLEAR)) ? p->selection : -1;
}
