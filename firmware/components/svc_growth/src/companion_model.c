#include "growth_engine.h"
#include <limits.h>
#include <stddef.h>
#include <string.h>

static const pet_companion_item_t items[] = {
    {"orbit-scarf", "轨道围巾", "30 金币", 30},
    {"star-halo", "星环耳饰", "等级 2 · 60 金币", 60},
    {"mail-bag", "小邮差包", "完成接星屑", 0},
    {"observer", "观测镜", "轨道对接 60 分", 0},
    {"headphones", "陪伴耳机", "完成一次专注", 0},
    {"antenna", "星光天线", "亲密度 20", 0},
    {"moon-lamp", "月球灯", "30 金币", 30},
    {"plant", "迷你盆栽", "等级 2 · 45 金币", 45},
    {"star-chart", "星图框", "信号接力 100 分", 0},
    {"rocket", "小火箭", "完成五局游戏", 0},
    {"trophy", "星屑奖杯", "接星屑 100 分", 0},
    {"notebook", "陪伴手账", "完成三次专注", 0},
};
const pet_companion_item_t *pet_companion_item(unsigned n)
{ return n && n <= PET_COMPANION_ITEMS ? &items[n - 1] : NULL; }
const char *pet_companion_theme(unsigned n)
{
    static const char *names[] = {"轨道休息舱", "月面温室", "深空观测室"};
    return n < 3 ? names[n] : "未知主题";
}
const char *pet_companion_memory_name(unsigned n)
{
    static const char *names[] = {"", "第一次照料", "成为亲密伙伴", "成长新阶段", "第一次游戏",
        "刷新个人纪录", "一起完成专注", "获得新收藏", "布置新小舱"};
    return n < sizeof(names) / sizeof(names[0]) ? names[n] : "共同回忆";
}

bool pet_companion_valid(const pet_companion_state_t *s)
{
    if (!s || s->owned > 4095 || s->appearance.theme > 2 || s->appearance.outfit > 6) return false;
    if (s->appearance.outfit && !(s->owned & (1u << (s->appearance.outfit - 1)))) return false;
    for (unsigned i = 0; i < 3; ++i) {
        unsigned n = s->appearance.slots[i];
        if (n && (n < 7 || n > 12 || !(s->owned & (1u << (n - 1))))) return false;
        if (n) for (unsigned j = 0; j < i; ++j) if (n == s->appearance.slots[j]) return false;
    }
    if (s->best[0] > 500 || s->best[1] > 250 || s->best[2] > 100) return false;
    for (unsigned i = 0; i < PET_COMPANION_MEMORIES; ++i) {
        const pet_memory_t *m = &s->memories[i];
        if (m->sequence > s->memory_sequence || m->kind > 8 || m->theme > 2 || m->outfit > 6 ||
            (m->sequence && (m->sequence % PET_COMPANION_MEMORIES != i || !m->kind))) return false;
    }
    unsigned count=s->memory_sequence<PET_COMPANION_MEMORIES?s->memory_sequence:PET_COMPANION_MEMORIES;
    for(unsigned i=0;i<count;++i) {
        uint32_t seq=s->memory_sequence-i;
        if(s->memories[seq%PET_COMPANION_MEMORIES].sequence!=seq)return false;
    }
    return true;
}
uint16_t pet_companion_eligible(const pet_growth_model_t *m)
{
    uint16_t bits = (1u << 0) | (1u << 6);
    if (m->level >= 2) bits |= (1u << 1) | (1u << 7);
    if (m->companion.played[0]) bits |= 1u << 2;
    if (m->companion.best[2] >= 60) bits |= 1u << 3;
    if (m->companion.focus_completed) bits |= 1u << 4;
    if (m->intimacy >= 20 * PET_GROWTH_UNITS) bits |= 1u << 5;
    if (m->companion.best[1] >= 100) bits |= 1u << 8;
    unsigned games = (unsigned)m->companion.played[0] + m->companion.played[1] + m->companion.played[2];
    if (games >= 5) bits |= 1u << 9;
    if (m->companion.best[0] >= 100) bits |= 1u << 10;
    if (m->companion.focus_completed >= 3) bits |= 1u << 11;
    return bits;
}
uint8_t pet_companion_themes(const pet_growth_model_t *m)
{
    unsigned games = (unsigned)m->companion.played[0] + m->companion.played[1] + m->companion.played[2];
    return (uint8_t)(1u | (m->level >= 3 || m->companion.focus_completed ? 2u : 0u) | (games >= 5 ? 4u : 0u));
}
void pet_companion_remember(pet_growth_model_t *m, unsigned kind, unsigned detail)
{
    pet_companion_state_t *s = &m->companion;
    if (!kind || kind > 8 || s->memory_sequence == UINT32_MAX) return;
    ++s->memory_sequence;
    s->memories[s->memory_sequence % PET_COMPANION_MEMORIES] = (pet_memory_t){
        s->memory_sequence, m->saved_epoch_s, (uint8_t)kind, (uint8_t)detail,
        s->appearance.theme, s->appearance.outfit};
}
static uint32_t request_hash(const pet_companion_request_t *r)
{
    const uint8_t bytes[] = {(uint8_t)r->operation, r->item, r->game, (uint8_t)r->score,
        (uint8_t)(r->score >> 8), r->practice, r->appearance.theme, r->appearance.outfit,
        r->appearance.slots[0], r->appearance.slots[1], r->appearance.slots[2],
        (uint8_t)r->expected_revision, (uint8_t)(r->expected_revision >> 8),
        (uint8_t)(r->expected_revision >> 16), (uint8_t)(r->expected_revision >> 24)};
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < sizeof(bytes); ++i) hash = (hash ^ bytes[i]) * 16777619u;
    return hash;
}
pet_companion_result_t pet_companion_apply(pet_growth_model_t *m, const pet_companion_request_t *r)
{
    pet_companion_result_t result = {PET_GROWTH_INVALID, false};
    if (!m || !r || !r->request_id || !pet_growth_model_valid(m) || (unsigned)r->operation > PET_COMPANION_GAME) return result;
    uint32_t hash = request_hash(r);
    bool remote = false;
    for (unsigned i = 0; i < 16; ++i) remote |= r->command_id[i] != 0;
    if (remote && r->operation != PET_COMPANION_DRESS) return result;
    if (remote && !memcmp(r->command_id, m->companion.last_command, 16)) {
        result.result = hash == m->companion.last_command_hash ? PET_GROWTH_DUPLICATE : PET_GROWTH_CONFLICT;
        return result;
    }
    if (r->request_id <= m->last_request_id) {
        if(remote) {result.result=PET_GROWTH_CONFLICT;return result;}
        result.result = r->request_id == m->last_request_id &&
            (m->last_action != PET_GROWTH_ACTION_COUNT || hash != m->companion.last_request_hash)
            ? PET_GROWTH_CONFLICT : PET_GROWTH_DUPLICATE;
        return result;
    }
    if (m->last_request_id == UINT64_MAX || r->request_id != m->last_request_id + 1) {
        result.result = PET_GROWTH_CONFLICT; return result;
    }
    pet_companion_state_t *s = &m->companion;
    if (r->operation == PET_COMPANION_BUY) {
        const pet_companion_item_t *item = pet_companion_item(r->item);
        if (!item) return result;
        uint16_t bit = (uint16_t)(1u << (r->item - 1));
        if (s->owned & bit) { result.result = PET_GROWTH_ALREADY_CLAIMED; return result; }
        if (!(pet_companion_eligible(m) & bit)) { result.result = PET_GROWTH_TASK_INCOMPLETE; return result; }
        if (m->coins < item->price) { result.result = PET_GROWTH_NO_COINS; return result; }
        m->coins -= item->price; s->owned |= bit;
        pet_companion_remember(m, 7, r->item);
    } else if (r->operation == PET_COMPANION_DRESS) {
        if (r->expected_revision != s->appearance_revision || s->appearance_revision == UINT32_MAX) {
            result.result = PET_GROWTH_CONFLICT; return result;
        }
        pet_companion_state_t candidate = *s;
        candidate.appearance = r->appearance;
        if (!pet_companion_valid(&candidate) || !(pet_companion_themes(m) & (1u << r->appearance.theme))) return result;
        bool changed_theme = s->appearance.theme != r->appearance.theme;
        s->appearance = r->appearance; ++s->appearance_revision;
        if (remote) { memcpy(s->last_command, r->command_id, 16); s->last_command_hash = hash; }
        if (changed_theme) pet_companion_remember(m, 8, r->appearance.theme);
    } else {
        static const unsigned max_scores[] = {500, 250, 100};
        if (r->game >= 3 || r->score > max_scores[r->game]) return result;
        if (!r->practice) {
            pet_growth_result_t reward = pet_growth_apply(m, PET_GROWTH_PLAY, r->request_id);
            if (reward == PET_GROWTH_OK) result.rewarded = true;
            else if (reward != PET_GROWTH_COOLDOWN && reward != PET_GROWTH_LOW_ENERGY && reward != PET_GROWTH_ASLEEP) {
                result.result = reward; return result;
            }
        }
        bool first = !s->played[r->game];
        if (s->played[r->game] < UINT16_MAX) ++s->played[r->game];
        if (first) pet_companion_remember(m, 4, r->game);
        if (r->score > s->best[r->game]) { s->best[r->game] = r->score; pet_companion_remember(m, 5, r->game); }
    }
    m->last_request_id = r->request_id; m->last_action = PET_GROWTH_ACTION_COUNT;
    s->last_request_hash = hash;
    result.result = PET_GROWTH_OK;
    return result;
}
