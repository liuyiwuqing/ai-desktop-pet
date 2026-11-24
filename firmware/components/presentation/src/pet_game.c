#include "pet/presentation/pet_game.h"
#include <stddef.h>
#include <string.h>

static uint32_t duration(const pet_game_t *g)
{ return g->kind == PET_GAME_STARS ? 45000u : g->kind == PET_GAME_SIGNAL ? 90000u : 60000u; }

static uint32_t random_next(pet_game_t *g)
{
    g->seed ^= g->seed << 13; g->seed ^= g->seed >> 17; g->seed ^= g->seed << 5;
    return g->seed;
}

static void new_round(pet_game_t *g)
{
    g->round_ms = g->elapsed_ms;
    g->matched = 0;
    if (g->kind == PET_GAME_STARS) {
        g->target = (uint8_t)(random_next(g) % 3u);
        g->debris = g->round % 4u == 3u;
    } else if (g->kind == PET_GAME_SIGNAL) {
        g->showing = true;
        for (unsigned i = 0; i < 7; ++i) g->sequence[i] = (uint8_t)(random_next(g) % 4u);
    }
}

bool pet_game_init(pet_game_t *g, pet_game_kind_t kind, uint32_t seed, uint32_t now)
{
    if (!g || (unsigned)kind >= PET_GAME_COUNT) return false;
    memset(g, 0, sizeof(*g));
    g->kind = kind; g->phase = PET_GAME_READY; g->last_ms = now;
    g->seed = seed ? seed : 1; g->shields = 3;
    new_round(g);
    return true;
}

uint32_t pet_game_remaining_ms(const pet_game_t *g)
{ return !g || g->elapsed_ms >= duration(g) ? 0 : duration(g) - g->elapsed_ms; }

unsigned pet_game_position(const pet_game_t *g)
{
    if (!g) return 0;
    if (g->kind == PET_GAME_STARS) return (g->elapsed_ms - g->round_ms) * 100u / 1100u;
    unsigned phase = (g->elapsed_ms - g->round_ms) % 2400u;
    unsigned position = (phase <= 1200 ? phase : 2400 - phase) / 12u;
    return g->reverse ? 100u - position : position;
}

void pet_game_tick(pet_game_t *g, uint32_t now)
{
    if (!g) return;
    uint32_t delta = now - g->last_ms;
    g->last_ms = now;
    if (g->phase != PET_GAME_RUNNING) return;
    /* Catch up with bounded simulation steps; a delayed frame cannot skip collisions. */
    uint32_t remaining = pet_game_remaining_ms(g);
    if (delta > remaining) delta = remaining;
    while (delta && g->phase == PET_GAME_RUNNING) {
        uint32_t step = delta > 50u ? 50u : delta;
        delta -= step; g->elapsed_ms += step;
        if (g->kind == PET_GAME_STARS && g->elapsed_ms - g->round_ms >= 1100u) {
            if (g->selection == g->target) {
                if (!g->debris) g->score += 10;
                else if (g->elapsed_ms >= g->shield_until_ms) g->score = g->score >= 5 ? g->score - 5 : 0;
            }
            ++g->round; new_round(g);
        } else if (g->kind == PET_GAME_SIGNAL && g->showing &&
                   g->elapsed_ms - g->round_ms >= (uint32_t)(g->round + 3u) * 650u + 400u) {
            g->showing = false;
        }
        if (!pet_game_remaining_ms(g)) g->phase = PET_GAME_RESULT;
    }
}

void pet_game_input(pet_game_t *g, pet_game_input_t input, uint32_t now)
{
    if (!g || (unsigned)input > PET_GAME_RESUME) return;
    pet_game_tick(g, now);
    if (input == PET_GAME_PAUSE && g->phase == PET_GAME_RUNNING) {
        g->phase = PET_GAME_PAUSED; return;
    }
    if (g->phase == PET_GAME_PAUSED) {
        if (input == PET_GAME_RESUME) g->phase = PET_GAME_RUNNING;
        return;
    }
    if (g->phase == PET_GAME_READY) {
        if (input == PET_GAME_CONFIRM) g->phase = PET_GAME_RUNNING;
        return;
    }
    if (g->phase != PET_GAME_RUNNING) return;
    if (input == PET_GAME_NEXT || input == PET_GAME_PREVIOUS) {
        if (g->kind == PET_GAME_ORBIT) g->reverse = !g->reverse;
        else if (!g->showing) {
            unsigned count = g->kind == PET_GAME_STARS ? 3u : 4u;
            g->selection = (uint8_t)((g->selection + (input == PET_GAME_NEXT ? 1u : count - 1u)) % count);
        }
    } else if (input == PET_GAME_CONFIRM) {
        if (g->kind == PET_GAME_STARS && g->shields) {
            --g->shields; g->shield_until_ms = g->elapsed_ms + 800u;
        } else if (g->kind == PET_GAME_SIGNAL && !g->showing) {
            bool correct = g->selection == g->sequence[g->matched];
            if (correct) { ++g->matched; g->score += 10; }
            if (!correct || g->matched == g->round + 3u) {
                if (++g->round == 5u) g->phase = PET_GAME_RESULT;
                else new_round(g);
            }
        } else if (g->kind == PET_GAME_ORBIT) {
            unsigned pos = pet_game_position(g);
            if (pos >= 40 && pos <= 60) g->score += 20;
            else if (pos >= 30 && pos <= 70) g->score += 10;
            if (++g->round == 5u) g->phase = PET_GAME_RESULT;
            else new_round(g);
        }
    }
}

const char *pet_game_name(pet_game_kind_t kind)
{
    static const char *names[] = {"接星屑", "信号接力", "轨道对接"};
    return (unsigned)kind < PET_GAME_COUNT ? names[kind] : "游乐场";
}
