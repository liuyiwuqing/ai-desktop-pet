#pragma once
#include "lvgl.h"
#include "pet/companion.h"
#include "pet/presentation/pet_game.h"
#include "pet_avatar.h"

typedef enum { PET_SPACE_CLOSED, PET_SPACE_GAME, PET_SPACE_WARDROBE, PET_SPACE_FURNITURE,
    PET_SPACE_THEMES, PET_SPACE_MEMORIES, PET_SPACE_ATTRIBUTES } pet_space_page_t;
typedef enum { PET_SPACE_CONFIRM, PET_SPACE_NEXT, PET_SPACE_PREVIOUS, PET_SPACE_BACK } pet_space_input_t;
typedef struct {
    lv_obj_t *root,*title,*status,*detail,*footer,*canvas;
    pet_avatar_t avatar;
    pet_game_t game;
    pet_space_page_t page;
    pet_companion_view_t snapshot;
    uint8_t selection,slot;
    bool choose_slot,pending,game_saved,game_attempted;
    const char *feedback;
    uint32_t frame_ms;
} pet_companion_ui_t;
void pet_companion_ui_init(pet_companion_ui_t *ui,lv_obj_t *root,const lv_font_t *small,const lv_font_t *large);
void pet_companion_ui_open(pet_companion_ui_t *ui,pet_space_page_t page,unsigned game,uint32_t now);
/* true returns one intent; caller assigns a request id and persists outside the LVGL owner. */
bool pet_companion_ui_input(pet_companion_ui_t *ui,pet_space_input_t input,uint32_t now,pet_companion_request_t *request);
void pet_companion_ui_show(pet_companion_ui_t *ui,const pet_growth_view_t *growth,const pet_companion_view_t *snapshot,uint32_t now);
void pet_companion_ui_complete(pet_companion_ui_t *ui,pet_companion_result_t result);
bool pet_companion_ui_tick(pet_companion_ui_t *ui,uint32_t now,bool visible);
void pet_companion_ui_pause(pet_companion_ui_t *ui,uint32_t now);
