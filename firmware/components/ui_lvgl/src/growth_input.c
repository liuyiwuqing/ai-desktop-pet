#include "growth_input.h"
#include "pet/growth.h"

#define PET_MENU_TIMEOUT_MS 30000u

static bool action_available(const pet_growth_input_t *p, uint8_t action)
{
    if (action >= PET_GROWTH_MENU_AI_TALK && action <= PET_GROWTH_MENU_AI_CLEAR)
        return p->voice_enabled;
    if (action == PET_GROWTH_MENU_CLIMATE) return p->climate_enabled;
    if (action == PET_GROWTH_MENU_NETWORK) return p->network_enabled;
    if (action >= PET_MENU_GAME_STARS && action <= PET_MENU_ATTRIBUTES) return !p->voice_only;
    if (action >= PET_GROWTH_MENU_RETURN) return true; /* 返回/消息/设置/系统不门控 */
    return !p->voice_only; /* 成长动作、成就与专注 */
}

static bool page_available(const pet_growth_input_t *p, uint8_t page);

bool pet_growth_input_item_available(const pet_growth_input_t *p, const pet_menu_item_t *item)
{
    return item->page == PET_MENU_NO_PAGE ? action_available(p, item->action) :
                                           page_available(p, item->page);
}

static bool page_available(const pet_growth_input_t *p, uint8_t page)
{
    const pet_menu_page_t *pg = pet_menu_page(page);
    if (!pg) return false;
    for (unsigned i = 0; i < pg->count; ++i)
        if (pet_growth_input_item_available(p, &pg->items[i])) return true;
    return false;
}

uint8_t pet_growth_input_selected_action(const pet_growth_input_t *p)
{
    const pet_menu_page_t *page = p->menu ? pet_menu_page(p->page) : NULL;
    if (!page || p->selection >= page->count) return PET_MENU_NO_ACTION;
    return page->items[p->selection].action;
}

void pet_growth_input_move(pet_growth_input_t *p, bool backwards, uint32_t now)
{
    const pet_menu_page_t *page = pet_menu_page(p->page);
    if (!p->menu || !page || page->count == 0) return;
    const unsigned count = page->count;
    unsigned guard = 0;
    do {
        p->selection = (uint8_t)((p->selection + count + (backwards ? count - 1 : 1)) % count);
    } while (++guard <= count && !pet_growth_input_item_available(p, &page->items[p->selection]));
    p->remembered[p->page]=p->selection;
    p->last_menu_ms = now;
}

bool pet_growth_input_open_menu(pet_growth_input_t *p, uint32_t now)
{
    if (p->menu) return false;
    p->menu = true;
    p->page = p->voice_enabled && p->voice_only ?
        (uint8_t)PET_MENU_AI : (uint8_t)PET_MENU_ROOT;
    p->selection = p->remembered[p->page];
    const pet_menu_page_t *page = pet_menu_page(p->page);
    unsigned guard = 0;
    while (page && guard++ < page->count &&
           !pet_growth_input_item_available(p, &page->items[p->selection]))
        p->selection = (uint8_t)((p->selection + 1) % page->count);
    p->last_menu_ms = now;
    return true;
}

int pet_growth_input_activate(pet_growth_input_t *p, uint32_t now)
{
    if (!p->menu) return -1;
    const pet_menu_page_t *page = pet_menu_page(p->page);
    if (!page || p->selection >= page->count) return -1;
    const pet_menu_item_t *item = &page->items[p->selection];
    p->last_menu_ms = now;
    if (item->page != PET_MENU_NO_PAGE) {
        if (!page_available(p, item->page)) return -1;
        p->remembered[p->page]=p->selection;
        p->page = item->page;
        p->selection = p->remembered[p->page];
        return -1;
    }
    /* PTT requires a genuinely held button; direction pushes cannot start capture. */
    if (!action_available(p, item->action) || item->action == PET_GROWTH_MENU_AI_TALK) return -1;
    p->remembered[p->page]=p->selection;
    bool keep = (item->action >= PET_MENU_VOLUME_UP && item->action <= PET_MENU_IDLE_BRIGHTNESS) ||
        item->action == PET_GROWTH_MENU_ACHIEVEMENTS ||
        (item->action >= PET_GROWTH_CLAIM_FEED && item->action <= PET_GROWTH_CLAIM_PLAY);
    if (!keep) { p->menu=false; p->page=PET_MENU_ROOT; p->selection=p->remembered[PET_MENU_ROOT]; }
    if (item->action == PET_GROWTH_MENU_ACHIEVEMENTS) return -1;
    return item->action;
}

bool pet_growth_input_menu_expired(pet_growth_input_t *p, uint32_t now)
{
    if (!p->menu || now - p->last_menu_ms < PET_MENU_TIMEOUT_MS) return false;
    p->menu = false;
    p->page = PET_MENU_ROOT;
    p->selection = 0;
    return true;
}

int pet_growth_input_navigate(pet_growth_input_t *p, pet_navigation_t direction,
                             bool blocked, uint32_t now)
{
    if (blocked) return pet_growth_input_update(p, p->raw, true, now);
    if (direction == PET_NAV_LEFT) {
        const bool cancel = p->holding || p->voice_active;
        p->holding = p->press_voice = false;
        p->armed = !p->raw;
        if (p->menu) {
            p->remembered[p->page]=p->selection;
            if (p->page == PET_MENU_ROOT) p->menu = false;
            p->page = PET_MENU_ROOT;
            p->selection=p->remembered[PET_MENU_ROOT];
        }
        return cancel ? PET_GROWTH_INPUT_AI_CANCEL : -1;
    }
    if (p->raw || p->stable || p->holding) return -1;
    if (!p->menu) {
        if (direction == PET_NAV_RIGHT) pet_growth_input_open_menu(p, now);
        return -1;
    }
    if (direction == PET_NAV_UP || direction == PET_NAV_DOWN)
        pet_growth_input_move(p, direction == PET_NAV_UP, now);
    else if (direction == PET_NAV_RIGHT)
        return pet_growth_input_activate(p, now);
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
        p->page = PET_MENU_ROOT;
        p->selection = 0;
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
    if (p->voice_enabled && p->menu &&
        pet_growth_input_selected_action(p) == PET_GROWTH_MENU_AI_TALK &&
        p->armed && p->stable && pressed && now - p->pressed_ms >= 800) {
        p->holding = true;
        p->menu = false;
        p->page = PET_MENU_ROOT;
        p->selection = 0;
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
            pet_growth_input_move(p, false, now);
        } else if (p->press_voice) return PET_GROWTH_INPUT_AI_CANCEL;
        return -1;
    }
    if (!p->menu) {
        pet_growth_input_open_menu(p, now);
        return -1;
    }
    /* 没有观察到持续按住的 TALK 不在松开时补录，避免“按完才开麦”。 */
    if (p->voice_enabled &&
        pet_growth_input_selected_action(p) == PET_GROWTH_MENU_AI_TALK) return -1;
    return pet_growth_input_activate(p, now);
}
