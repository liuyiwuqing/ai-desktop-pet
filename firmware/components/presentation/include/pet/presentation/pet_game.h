#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { PET_GAME_STARS, PET_GAME_SIGNAL, PET_GAME_ORBIT, PET_GAME_COUNT } pet_game_kind_t;
typedef enum { PET_GAME_READY, PET_GAME_RUNNING, PET_GAME_PAUSED, PET_GAME_RESULT } pet_game_phase_t;
typedef enum { PET_GAME_CONFIRM, PET_GAME_NEXT, PET_GAME_PREVIOUS, PET_GAME_PAUSE, PET_GAME_RESUME } pet_game_input_t;

/* Fixed-size, UI-owned game state. No storage, audio, allocation, or rewards here.
 * The same CONFIRM/NEXT inputs work with three keys and the optional joystick. */
typedef struct {
    pet_game_kind_t kind;
    pet_game_phase_t phase;
    uint32_t last_ms, elapsed_ms, round_ms, shield_until_ms, seed;
    uint16_t score;
    uint8_t selection, target, round, shields, sequence[7], matched;
    bool showing, debris, reverse;
} pet_game_t;

bool pet_game_init(pet_game_t *game, pet_game_kind_t kind, uint32_t seed, uint32_t now_ms);
void pet_game_tick(pet_game_t *game, uint32_t now_ms);
void pet_game_input(pet_game_t *game, pet_game_input_t input, uint32_t now_ms);
uint32_t pet_game_remaining_ms(const pet_game_t *game);
unsigned pet_game_position(const pet_game_t *game);
const char *pet_game_name(pet_game_kind_t kind);
