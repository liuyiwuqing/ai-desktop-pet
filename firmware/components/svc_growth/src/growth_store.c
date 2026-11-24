#include "growth_store.h"

#include <string.h>
#include "nvs.h"
#include "pet/storage.h"

#define SAVE_SIZE 256
#define ACTIVE_SIZE 16
#define STORE_NS "pet_growth"

static const char *slot_key(unsigned slot) { return slot == 0 ? "snap0" : "snap1"; }

static void put(uint8_t *p, uint64_t value, unsigned size)
{
    for (unsigned i = 0; i < size; ++i) p[i] = (uint8_t)(value >> (8 * i));
}
static uint64_t get(const uint8_t *p, unsigned size)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < size; ++i) value |= (uint64_t)p[i] << (8 * i);
    return value;
}
static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ ((crc & 1) ? UINT32_C(0xedb88320) : 0);
    }
    return ~crc;
}

/* v5 为 256 B；前 104 B 继承 v4，新增收藏、外观与回忆。兼容 v1–v4。
 * 小端，CRC 覆盖整个记录，CRC 字段本身按零处理。
 * 字段偏移是磁盘契约，不使用 packed 结构或编译器内存布局。 */
static void encode(const pet_growth_model_t *m, uint32_t revision, uint8_t *out)
{
    memset(out, 0, SAVE_SIZE);
    memcpy(out, "PETG", 4);
    put(out + 4, 5, 2); put(out + 6, SAVE_SIZE, 2); put(out + 8, revision, 4);
    put(out + 16, m->last_request_id, 8);
    put(out + 24, m->exp, 4); put(out + 28, m->coins, 4);
    put(out + 32, m->saved_epoch_s, 4); put(out + 36, m->daily_day, 4);
    put(out + 40, m->inactive_s, 4); put(out + 44, m->intimacy, 4);
    put(out + 48, (uint32_t)m->mood, 4); put(out + 52, m->energy, 4);
    put(out + 56, m->hunger, 4); put(out + 60, m->health, 4);
    put(out + 64, m->feed_cooldown_s, 2); put(out + 66, m->play_cooldown_s, 2);
    put(out + 68, m->pet_count_today, 2); put(out + 70, m->neglect_s, 4);
    out[74] = m->level; out[75] = m->pet_cooldown_s;
    out[76] = m->last_action; out[77] = m->sleeping;
    out[78] = m->achievements;
    memcpy(out + 80, m->task_progress, 3);
    out[83] = m->task_claimed;
    put(out+84,m->last_dialogue_event,8); put(out+92,m->last_focus_event,8);
    out[100]=m->dialogues_today; out[101]=m->focus_today;
    const pet_companion_state_t *c = &m->companion;
    put(out+104,c->owned,2); put(out+106,c->focus_completed,2);
    for (unsigned i=0;i<3;++i) { put(out+108+2*i,c->best[i],2); put(out+114+2*i,c->played[i],2); }
    out[120]=c->appearance.theme; out[121]=c->appearance.outfit;
    memcpy(out+122,c->appearance.slots,3);
    put(out+125,c->appearance_revision,4); put(out+129,c->memory_sequence,4);
    put(out+133,c->last_request_hash,4); put(out+137,c->last_command_hash,4);
    memcpy(out+141,c->last_command,16);
    for (unsigned i=0;i<PET_COMPANION_MEMORIES;++i) {
        uint8_t *p=out+157+12*i; const pet_memory_t *v=&c->memories[i];
        put(p,v->sequence,4); put(p+4,v->epoch,4);
        p[8]=v->kind; p[9]=v->detail; p[10]=v->theme; p[11]=v->outfit;
    }
    put(out + 12, crc32(out, SAVE_SIZE), 4);
}

static pet_growth_result_t decode(uint8_t *data, size_t size, pet_growth_model_t *m,
                                  uint32_t *revision)
{
    if (size < 8 || memcmp(data, "PETG", 4) != 0) return PET_GROWTH_CORRUPT_SAVE;
    unsigned version = (unsigned)get(data + 4, 2);
    if (version != 1 && version != 2 && version != 3 && version != 4 && version != 5) return PET_GROWTH_UNKNOWN_VERSION;
    if (size != (version == 1 ? 80 : version < 4 ? 84 : version == 4 ? 104 : SAVE_SIZE) || get(data + 6, 2) != size)
        return PET_GROWTH_CORRUPT_SAVE;
    uint32_t expected = (uint32_t)get(data + 12, 4);
    put(data + 12, 0, 4);
    if (crc32(data, size) != expected || data[77] > 1 || (version < 3 && data[78]) || data[79])
        return PET_GROWTH_CORRUPT_SAVE;
    memset(m, 0, sizeof(*m));
    m->last_request_id = get(data + 16, 8);
    m->exp = (uint32_t)get(data + 24, 4); m->coins = (uint32_t)get(data + 28, 4);
    m->saved_epoch_s = (uint32_t)get(data + 32, 4); m->daily_day = (uint32_t)get(data + 36, 4);
    m->inactive_s = (uint32_t)get(data + 40, 4); m->intimacy = (uint32_t)get(data + 44, 4);
    /* 通过 int64_t 解释二补码，避免超出 int32_t 的无符号转有符号实现差异。 */
    uint32_t mood = (uint32_t)get(data + 48, 4);
    m->mood = (int32_t)(mood <= INT32_MAX ? (int64_t)mood : (int64_t)mood - INT64_C(4294967296));
    m->energy = (uint32_t)get(data + 52, 4); m->hunger = (uint32_t)get(data + 56, 4);
    m->health = (uint32_t)get(data + 60, 4);
    m->feed_cooldown_s = (uint16_t)get(data + 64, 2); m->play_cooldown_s = (uint16_t)get(data + 66, 2);
    m->pet_count_today = (uint16_t)get(data + 68, 2); m->neglect_s = (uint32_t)get(data + 70, 4);
    m->level = data[74]; m->pet_cooldown_s = data[75];
    m->last_action = data[76]; m->sleeping = data[77] != 0;
    if (version >= 2) {
        memcpy(m->task_progress, data + 80, 3);
        m->task_claimed = data[83];
    } else if (m->last_action > PET_GROWTH_WAKE) return PET_GROWTH_CORRUPT_SAVE;
    if (version >= 3) m->achievements = data[78];
    if (version>=4) {
        m->last_dialogue_event=get(data+84,8); m->last_focus_event=get(data+92,8);
        m->dialogues_today=data[100];m->focus_today=data[101];
        if (data[102]||data[103]) return PET_GROWTH_CORRUPT_SAVE;
    }
    if (version == 5) {
        pet_companion_state_t *c=&m->companion;
        c->owned=(uint16_t)get(data+104,2); c->focus_completed=(uint16_t)get(data+106,2);
        for (unsigned i=0;i<3;++i) { c->best[i]=(uint16_t)get(data+108+2*i,2); c->played[i]=(uint16_t)get(data+114+2*i,2); }
        c->appearance.theme=data[120]; c->appearance.outfit=data[121]; memcpy(c->appearance.slots,data+122,3);
        c->appearance_revision=(uint32_t)get(data+125,4); c->memory_sequence=(uint32_t)get(data+129,4);
        c->last_request_hash=(uint32_t)get(data+133,4); c->last_command_hash=(uint32_t)get(data+137,4);
        memcpy(c->last_command,data+141,16);
        for (unsigned i=0;i<PET_COMPANION_MEMORIES;++i) {
            const uint8_t *p=data+157+12*i; pet_memory_t *v=&c->memories[i];
            v->sequence=(uint32_t)get(p,4); v->epoch=(uint32_t)get(p+4,4);
            v->kind=p[8]; v->detail=p[9]; v->theme=p[10]; v->outfit=p[11];
        }
        if (data[253]||data[254]||data[255]) return PET_GROWTH_CORRUPT_SAVE;
    } else if (m->last_action >= PET_GROWTH_ACTION_COUNT) return PET_GROWTH_CORRUPT_SAVE;
    *revision = (uint32_t)get(data + 8, 4);
    return *revision != 0 && pet_growth_model_valid(m) ? PET_GROWTH_OK : PET_GROWTH_CORRUPT_SAVE;
}

pet_growth_result_t pet_growth_backup_encode(const pet_growth_model_t *model,uint32_t revision,
    void *out,size_t capacity,size_t *bytes)
{
    if (!out||!bytes||capacity<SAVE_SIZE||!revision||!pet_growth_model_valid(model))
        return PET_GROWTH_INVALID;
    encode(model,revision,out);*bytes=SAVE_SIZE;return PET_GROWTH_OK;
}
pet_growth_result_t pet_growth_backup_decode(const void *data,size_t bytes,pet_growth_model_t *out)
{
    if (!data||!out||bytes>256) return PET_GROWTH_INVALID;
    uint8_t copy[256];memcpy(copy,data,bytes);uint32_t revision;
    return decode(copy,bytes,out,&revision);
}

pet_growth_result_t pet_growth_store_load(pet_growth_store_t *store, pet_growth_model_t *model)
{
    *store = (pet_growth_store_t){.active_slot = -1};
    if (pet_storage_init() != ESP_OK) return PET_GROWTH_STORAGE_ERROR;
    uint8_t active[ACTIVE_SIZE];
    size_t length = 0;
    esp_err_t active_err = pet_storage_load(STORE_NS, "active", active, sizeof(active), &length);
    bool missing = active_err == ESP_ERR_NVS_NOT_FOUND;
    bool valid_active = active_err == ESP_OK && length == ACTIVE_SIZE &&
        memcmp(active, "PGAC", 4) == 0 && get(active + 8, 4) <= 1 &&
        get(active + 4, 4) != 0 && crc32(active, 12) == get(active + 12, 4);
    pet_growth_model_t candidates[2];
    uint32_t revisions[2] = {0};
    pet_growth_result_t results[2];
    bool absent[2];
    for (unsigned i = 0; i < 2; ++i) {
        /* 多留空间以识别未来格式的版本号；超过缓冲区同样拒绝覆盖。 */
        uint8_t data[256];
        length = 0;
        esp_err_t err = pet_storage_load(STORE_NS, slot_key(i), data, sizeof(data), &length);
        absent[i] = err == ESP_ERR_NVS_NOT_FOUND;
        results[i] = err == ESP_OK ? decode(data, length, &candidates[i], &revisions[i])
                                  : PET_GROWTH_STORAGE_ERROR;
        if (results[i] == PET_GROWTH_UNKNOWN_VERSION) return results[i];
        if (err != ESP_OK && !absent[i]) return PET_GROWTH_STORAGE_ERROR;
    }
    if (missing && absent[0] && absent[1]) {
        pet_growth_defaults(model);
        return PET_GROWTH_OK;
    }
    if (!valid_active) return active_err == ESP_OK || missing ? PET_GROWTH_CORRUPT_SAVE
                                                             : PET_GROWTH_STORAGE_ERROR;
    unsigned slot = (unsigned)get(active + 8, 4);
    uint32_t committed = (uint32_t)get(active + 4, 4);
    if (results[slot] != PET_GROWTH_OK || revisions[slot] != committed) {
        slot ^= 1;
        /* 较新的非活动槽可能只是未提交候选，不能当作成功互动恢复。 */
        if (results[slot] != PET_GROWTH_OK || revisions[slot] >= committed)
            return PET_GROWTH_CORRUPT_SAVE;
        store->recovered = true;
    }
    *model = candidates[slot];
    store->active_slot = (int8_t)slot;
    store->revision = committed;
    return PET_GROWTH_OK;
}

pet_growth_result_t pet_growth_store_save(pet_growth_store_t *store, const pet_growth_model_t *model)
{
    if (store->write_uncertain) return PET_GROWTH_STORAGE_UNCERTAIN;
    if (!pet_growth_model_valid(model) || store->revision == UINT32_MAX)
        return PET_GROWTH_STORAGE_ERROR;
    unsigned slot = store->active_slot < 0 ? 0 : (unsigned)(store->active_slot ^ 1);
    uint32_t revision = store->revision + 1;
    uint8_t data[SAVE_SIZE], verify[SAVE_SIZE];
    encode(model, revision, data);
    if (pet_storage_save(STORE_NS, slot_key(slot), data, sizeof(data)) != ESP_OK)
        return PET_GROWTH_STORAGE_ERROR;
    size_t length = 0;
    if (pet_storage_load(STORE_NS, slot_key(slot), verify, sizeof(verify), &length) != ESP_OK ||
        length != sizeof(data) || memcmp(data, verify, sizeof(data)) != 0)
        return PET_GROWTH_STORAGE_ERROR;
    uint8_t active[ACTIVE_SIZE] = {0};
    memcpy(active, "PGAC", 4);
    put(active + 4, revision, 4); put(active + 8, slot, 4);
    put(active + 12, crc32(active, 12), 4);
    (void)pet_storage_save(STORE_NS, "active", active, sizeof(active));
    uint8_t confirmed[ACTIVE_SIZE];
    length = 0;
    esp_err_t err = pet_storage_load(STORE_NS, "active", confirmed, sizeof(confirmed), &length);
    if (err != ESP_OK || length != sizeof(active) || memcmp(active, confirmed, sizeof(active)) != 0) {
        bool previous_still_active = err == ESP_OK && length == ACTIVE_SIZE &&
            memcmp(confirmed, "PGAC", 4) == 0 && get(confirmed + 4, 4) == store->revision &&
            get(confirmed + 8, 4) <= 1 && crc32(confirmed, 12) == get(confirmed + 12, 4);
        if (previous_still_active || (err == ESP_ERR_NVS_NOT_FOUND && store->active_slot < 0))
            return PET_GROWTH_STORAGE_ERROR;
        /* 提交结果无法确认时停止后续写入；重新加载后用原 request_id 查询/重试，
         * 防止旧 RAM 状态继续覆盖一份实际已成功的提交。 */
        store->write_uncertain = true;
        return PET_GROWTH_STORAGE_UNCERTAIN;
    }
    store->active_slot = (int8_t)slot;
    store->revision = revision;
    return PET_GROWTH_OK;
}
