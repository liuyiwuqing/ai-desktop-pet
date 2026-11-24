#include <assert.h>
#include <stdio.h>
#include "pet/presentation/pet_game.h"

int main(void)
{
    pet_game_t game;
    assert(pet_game_init(&game, PET_GAME_STARS, 42, 100));
    pet_game_input(&game, PET_GAME_CONFIRM, 100);
    pet_game_tick(&game, 1100);
    assert(game.phase == PET_GAME_RUNNING && pet_game_remaining_ms(&game) == 44000);
    pet_game_input(&game, PET_GAME_PAUSE, 1100);
    pet_game_tick(&game, 61100);
    assert(game.phase == PET_GAME_PAUSED && pet_game_remaining_ms(&game) == 44000);
    pet_game_input(&game, PET_GAME_RESUME, 61100);
    pet_game_tick(&game, 105100);
    assert(game.phase == PET_GAME_RESULT && pet_game_remaining_ms(&game) == 0);
    puts("pet game: pause freezes the game clock and resumes without hidden progress");

    /* The complete memory game is playable with only next + confirm. */
    assert(pet_game_init(&game, PET_GAME_SIGNAL, 7, 0));
    pet_game_input(&game, PET_GAME_CONFIRM, 0);
    uint32_t now = 0;
    for (unsigned round = 0; round < 5; ++round) {
        uint16_t before = game.score;
        pet_game_input(&game, PET_GAME_CONFIRM, now); /* Cannot answer during demonstration. */
        assert(game.score == before);
        now += (round + 3) * 650 + 400;
        pet_game_tick(&game, now);
        assert(!game.showing);
        uint8_t sequence[7];
        for (unsigned i = 0; i < round + 3; ++i) sequence[i] = game.sequence[i];
        for (unsigned i = 0; i < round + 3; ++i) {
            while (game.selection != sequence[i]) pet_game_input(&game, PET_GAME_NEXT, now);
            pet_game_input(&game, PET_GAME_CONFIRM, now);
        }
    }
    assert(game.phase == PET_GAME_RESULT && game.score == 250);
    pet_game_input(&game, PET_GAME_CONFIRM, now);
    assert(game.score == 250); /* A result screen cannot award another hit. */

    assert(pet_game_init(&game, PET_GAME_ORBIT, 7, UINT32_MAX - 100));
    now = UINT32_MAX - 100;
    pet_game_input(&game, PET_GAME_CONFIRM, now);
    for (unsigned round = 0; round < 5; ++round) {
        now += 600;
        pet_game_input(&game, PET_GAME_CONFIRM, now);
    }
    assert(game.phase == PET_GAME_RESULT && game.score == 100);

    assert(pet_game_init(&game, PET_GAME_STARS, 9, 0));
    pet_game_input(&game, PET_GAME_CONFIRM, 0);
    pet_game_input(&game, PET_GAME_NEXT, 0);
    assert(game.selection == 1);
    pet_game_input(&game, PET_GAME_PREVIOUS, 0);
    assert(game.selection == 0);
    for (unsigned i = 0; i < 6; ++i) pet_game_input(&game, PET_GAME_CONFIRM, 0);
    assert(game.shields == 0);
    pet_game_tick(&game, UINT32_MAX);
    assert(game.phase == PET_GAME_RESULT && game.elapsed_ms == 45000);
    assert(!pet_game_init(&game, PET_GAME_COUNT, 1, 0));
    assert(!pet_game_init(NULL, PET_GAME_STARS, 1, 0));
    pet_game_tick(NULL, 0); pet_game_input(NULL, PET_GAME_NEXT, 0);
    puts("pet game: three-key completion, score bounds, held time, wraparound and invalid input passed");
}
