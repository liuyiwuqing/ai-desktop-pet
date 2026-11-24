#include "svc_growth/api.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "pet/event_bus.h"
#include "growth_store.h"
#include "growth_environment.h"

static struct {
    SemaphoreHandle_t mutex;
    pet_growth_clock_t clock;
    pet_growth_model_t model;
    pet_growth_store_t store;
    pet_growth_environment_policy_t environment;
    uint64_t settled_ms;
    uint64_t save_attempt_ms;
    uint64_t excited_until_ms;
    bool waiting_for_time;
    bool dirty;
} s;

static uint64_t now_ms(void) { return s.clock.monotonic_ms(s.clock.context); }

static pet_growth_view_t view_locked(void)
{
    uint16_t minute = UINT16_MAX;
    if (s.model.saved_epoch_s) {
        int64_t local_s = (int64_t)s.model.saved_epoch_s + (int64_t)s.clock.utc_offset_min * 60;
        minute = (uint16_t)(((local_s % 86400 + 86400) % 86400) / 60);
    }
    pet_growth_view_t view = pet_growth_view(&s.model, minute, now_ms() < s.excited_until_ms);
    view.revision = s.store.revision;
    view.time_ready = !s.waiting_for_time && now_ms() >= s.settled_ms;
    view.unsaved = s.dirty;
    view.storage_ready = !s.store.write_uncertain;
    view.save_recovered = s.store.recovered;
    view.environment = (uint8_t)pet_growth_environment_current(&s.environment, now_ms());
    return view;
}

static void publish_locked(void)
{
    pet_growth_view_t view = view_locked();
    pet_event_t event;
    pet_event_prepare(&event, PET_EVT_GROWTH_CHANGED, PET_EVENT_SOURCE_GROWTH, PET_EVENT_PRIORITY_NORMAL);
    event.timestamp_ms = (uint32_t)now_ms();
    event.payload_len = sizeof(view);
    memcpy(event.payload, &view, sizeof(view));
    /* 队列满不回滚已经提交的互动；消费者可随时重新读取快照。 */
    (void)pet_event_bus_publish(&event);
}

static pet_growth_result_t commit_locked(const pet_growth_model_t *candidate, uint64_t now)
{
    s.save_attempt_ms = now;
    pet_growth_result_t result = pet_growth_store_save(&s.store, candidate);
    if (result != PET_GROWTH_OK) return result;
    s.model = *candidate;
    s.dirty = false;
    return PET_GROWTH_OK;
}

/* 恢复时只结算一次离线区间；成功落盘后才允许互动。时间回拨以存档时间为锚，
 * 运行中随后只用单调时间推进，避免 SNTP 重校时重复衰减或刷新每日上限。 */
static pet_growth_result_t restore_time_locked(uint32_t epoch, uint64_t now)
{
    pet_growth_model_t candidate = s.model;
    if (candidate.saved_epoch_s && epoch > candidate.saved_epoch_s) {
        uint32_t elapsed = epoch - candidate.saved_epoch_s;
        pet_growth_advance_offline(&candidate, elapsed > PET_GROWTH_OFFLINE_MAX_S ? PET_GROWTH_OFFLINE_MAX_S : elapsed);
    }
    if (epoch > candidate.saved_epoch_s) candidate.saved_epoch_s = epoch;
    pet_growth_calendar(&candidate, candidate.saved_epoch_s, s.clock.utc_offset_min);
    pet_growth_result_t result = commit_locked(&candidate, now);
    if (result == PET_GROWTH_OK) {
        s.waiting_for_time = false;
        s.settled_ms = now;
        publish_locked();
    }
    return result;
}

pet_growth_result_t pet_growth_init(const pet_growth_clock_t *clock)
{
    if (!clock || !clock->monotonic_ms || !clock->epoch_s ||
        clock->utc_offset_min < -720 || clock->utc_offset_min > 840) return PET_GROWTH_INVALID;
    if (s.mutex) return PET_GROWTH_CONFLICT;
    s.clock = *clock;
    pet_growth_result_t result = pet_growth_store_load(&s.store, &s.model);
    if (result != PET_GROWTH_OK) return result;
    s.mutex = xSemaphoreCreateMutex();
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    s.settled_ms = s.save_attempt_ms = now_ms();
    uint32_t epoch = s.clock.epoch_s(s.clock.context);
    s.waiting_for_time = s.model.saved_epoch_s != 0 && epoch == 0;
    if (epoch && (epoch != s.model.saved_epoch_s || s.store.active_slot < 0))
        result = restore_time_locked(epoch, s.settled_ms);
    else if (s.store.active_slot < 0)
        result = commit_locked(&s.model, s.settled_ms);
    if (result != PET_GROWTH_OK) pet_growth_close();
    return result;
}

void pet_growth_close(void)
{
    if (s.mutex) vSemaphoreDelete(s.mutex);
    memset(&s, 0, sizeof(s));
}

static pet_growth_result_t advance_locked(uint64_t now)
{
    uint32_t epoch = s.clock.epoch_s(s.clock.context);
    if (s.waiting_for_time)
        return epoch ? restore_time_locked(epoch, now) : PET_GROWTH_TIME_UNAVAILABLE;
    if (now < s.settled_ms || now < s.save_attempt_ms) return PET_GROWTH_TIME_UNAVAILABLE;
    uint64_t elapsed = (now - s.settled_ms) / 1000;
    if (elapsed) {
        /* 长时间未调度按恢复处理，同样最多结算八小时，避免无界循环。 */
        uint32_t step = elapsed > PET_GROWTH_OFFLINE_MAX_S ? PET_GROWTH_OFFLINE_MAX_S : (uint32_t)elapsed;
        uint32_t exposed = pet_growth_environment_consume(&s.environment, now, step);
        pet_growth_advance_environment(&s.model, step, exposed);
        s.settled_ms += elapsed * 1000;
        if (s.model.saved_epoch_s) {
            uint64_t projected = (uint64_t)s.model.saved_epoch_s + elapsed;
            s.model.saved_epoch_s = projected > UINT32_MAX ? UINT32_MAX : (uint32_t)projected;
        }
        s.dirty = true;
    }
    if (!s.model.saved_epoch_s && epoch) {
        /* 新档允许未对时离线玩；首次对时只建立时间锚，不再补算已运行时间。 */
        s.model.saved_epoch_s = epoch;
        s.dirty = true;
    }
    pet_growth_calendar(&s.model, s.model.saved_epoch_s, s.clock.utc_offset_min);
    return PET_GROWTH_OK;
}

pet_growth_result_t pet_growth_export(void *out,size_t capacity,size_t *bytes,uint32_t *revision)
{
    if (!revision) return PET_GROWTH_INVALID;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex,portMAX_DELAY);
    pet_growth_result_t result=s.store.write_uncertain?PET_GROWTH_STORAGE_UNCERTAIN:
        pet_growth_backup_encode(&s.model,s.store.revision,out,capacity,bytes);
    if (result==PET_GROWTH_OK) *revision=s.store.revision;
    xSemaphoreGive(s.mutex);return result;
}

pet_growth_result_t pet_growth_import(const void *data,size_t bytes,uint32_t expected_revision)
{
    pet_growth_model_t candidate;
    pet_growth_result_t result=pet_growth_backup_decode(data,bytes,&candidate);
    if (result!=PET_GROWTH_OK) return result;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex,portMAX_DELAY);
    if (s.store.revision!=expected_revision) result=PET_GROWTH_CONFLICT;
    else if (s.store.write_uncertain) result=PET_GROWTH_STORAGE_UNCERTAIN;
    else {
        /* 只恢复照料数值；当前已消费的事件/日期/奖励标记不能因回档倒退。 */
        if (candidate.last_request_id<s.model.last_request_id) {
            candidate.last_request_id=s.model.last_request_id;candidate.last_action=s.model.last_action;
        }
        if (candidate.last_dialogue_event<s.model.last_dialogue_event) candidate.last_dialogue_event=s.model.last_dialogue_event;
        if (candidate.last_focus_event<s.model.last_focus_event) candidate.last_focus_event=s.model.last_focus_event;
        candidate.achievements|=s.model.achievements;
        /* 回档不退还已花金币，也不撤销收藏、回忆或已应用的远端外观命令。 */
        if (candidate.coins > s.model.coins) candidate.coins=s.model.coins;
        pet_companion_state_t restored=candidate.companion;
        candidate.companion=s.model.companion;
        candidate.companion.owned|=restored.owned;
        if (restored.focus_completed>candidate.companion.focus_completed) candidate.companion.focus_completed=restored.focus_completed;
        for (unsigned i=0;i<3;++i) {
            if (restored.best[i]>candidate.companion.best[i]) candidate.companion.best[i]=restored.best[i];
            if (restored.played[i]>candidate.companion.played[i]) candidate.companion.played[i]=restored.played[i];
        }
        if (candidate.saved_epoch_s<s.model.saved_epoch_s) candidate.saved_epoch_s=s.model.saved_epoch_s;
        if (candidate.daily_day<s.model.daily_day) {
            candidate.daily_day=s.model.daily_day;memcpy(candidate.task_progress,s.model.task_progress,3);
            candidate.task_claimed=s.model.task_claimed;candidate.pet_count_today=s.model.pet_count_today;
            candidate.dialogues_today=s.model.dialogues_today;candidate.focus_today=s.model.focus_today;
        } else if (candidate.daily_day==s.model.daily_day) {
            candidate.task_claimed|=s.model.task_claimed;
            if (candidate.pet_count_today<s.model.pet_count_today) candidate.pet_count_today=s.model.pet_count_today;
            if (candidate.dialogues_today<s.model.dialogues_today) candidate.dialogues_today=s.model.dialogues_today;
            if (candidate.focus_today<s.model.focus_today) candidate.focus_today=s.model.focus_today;
        }
        if (candidate.feed_cooldown_s<s.model.feed_cooldown_s) candidate.feed_cooldown_s=s.model.feed_cooldown_s;
        if (candidate.play_cooldown_s<s.model.play_cooldown_s) candidate.play_cooldown_s=s.model.play_cooldown_s;
        if (candidate.pet_cooldown_s<s.model.pet_cooldown_s) candidate.pet_cooldown_s=s.model.pet_cooldown_s;
        result=commit_locked(&candidate,now_ms());
        if (result==PET_GROWTH_OK) {s.settled_ms=now_ms();s.waiting_for_time=candidate.saved_epoch_s&& !s.clock.epoch_s(s.clock.context);publish_locked();}
    }
    xSemaphoreGive(s.mutex);return result;
}

pet_growth_result_t pet_growth_interact(const pet_growth_request_t *request)
{
    if (!request || !request->request_id || (unsigned)request->action >= PET_GROWTH_ACTION_COUNT)
        return PET_GROWTH_INVALID;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    pet_growth_model_t candidate = s.model;
    pet_growth_result_t result;
    uint64_t now = now_ms();
    if (request->request_id <= s.model.last_request_id) {
        result = pet_growth_apply(&candidate, request->action, request->request_id);
    } else if (s.store.write_uncertain) {
        result = PET_GROWTH_STORAGE_UNCERTAIN;
    } else if ((now >= request->issued_at_ms ? now - request->issued_at_ms : request->issued_at_ms - now) > 300000) {
        result = PET_GROWTH_STALE_REQUEST;
    } else {
        result = advance_locked(now);
        if (result == PET_GROWTH_OK) {
            candidate = s.model;
            result = request->action >= PET_GROWTH_CLAIM_FEED &&
                     request->task_day != candidate.daily_day ? PET_GROWTH_STALE_REQUEST :
                     pet_growth_apply(&candidate, request->action, request->request_id);
            if (result == PET_GROWTH_OK) {
                bool celebrate = candidate.level > s.model.level || candidate.achievements != s.model.achievements;
                result = commit_locked(&candidate, now);
                if (result == PET_GROWTH_OK) {
                    if (celebrate) s.excited_until_ms = now <= UINT64_MAX - 3000 ? now + 3000 : UINT64_MAX;
                    publish_locked();
                }
            }
        }
    }
    xSemaphoreGive(s.mutex);
    return result;
}

static pet_growth_result_t settle(bool force)
{
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    uint64_t now = now_ms();
    pet_growth_result_t result = advance_locked(now);
    if (result == PET_GROWTH_OK && s.dirty && (force || now - s.save_attempt_ms >= 300000)) {
        result = commit_locked(&s.model, now);
        if (result == PET_GROWTH_OK) publish_locked();
    }
    xSemaphoreGive(s.mutex);
    return result;
}

pet_growth_result_t pet_growth_tick(void) { return settle(false); }
pet_growth_result_t pet_growth_flush(void) { return settle(true); }

pet_growth_result_t pet_growth_observe_climate(const pet_climate_reading_t *reading)
{
    if (!reading) return PET_GROWTH_INVALID;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex, portMAX_DELAY);
    uint64_t now = now_ms();
    pet_growth_result_t result = s.store.write_uncertain ? PET_GROWTH_STORAGE_UNCERTAIN : advance_locked(now);
    if (result == PET_GROWTH_OK)
        result = pet_growth_environment_observe(&s.environment, reading, now);
    xSemaphoreGive(s.mutex);
    return result;
}

pet_growth_result_t pet_growth_get_snapshot(pet_growth_view_t *out)
{
    if (!out) return PET_GROWTH_INVALID;
    if (!s.mutex || xSemaphoreTake(s.mutex, 0) != pdTRUE) return PET_GROWTH_NOT_READY;
    *out = view_locked();
    xSemaphoreGive(s.mutex);
    return PET_GROWTH_OK;
}

pet_growth_result_t pet_growth_set_utc_offset(int16_t minutes)
{
    if (minutes < -720 || minutes > 840) return PET_GROWTH_INVALID;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex,portMAX_DELAY); s.clock.utc_offset_min=minutes; xSemaphoreGive(s.mutex);
    return PET_GROWTH_OK;
}

pet_growth_result_t pet_growth_reward(pet_growth_reward_t kind,uint64_t event_id,uint32_t day)
{
    if (!event_id || (kind!=PET_GROWTH_REWARD_DIALOGUE && kind!=PET_GROWTH_REWARD_FOCUS)) return PET_GROWTH_INVALID;
    if (!s.mutex) return PET_GROWTH_NOT_READY;
    xSemaphoreTake(s.mutex,portMAX_DELAY);
    pet_growth_result_t result=s.store.write_uncertain?PET_GROWTH_STORAGE_UNCERTAIN:advance_locked(now_ms());
    if (result==PET_GROWTH_OK) {
        if (day!=s.model.daily_day) result=PET_GROWTH_STALE_REQUEST;
        else {
            pet_growth_model_t candidate=s.model;
            result=pet_growth_award_completion(&candidate,kind==PET_GROWTH_REWARD_FOCUS,event_id);
            if (result==PET_GROWTH_OK) {
                result=commit_locked(&candidate,now_ms());
                if (result==PET_GROWTH_OK) publish_locked();
            }
        }
    }
    xSemaphoreGive(s.mutex);return result;
}

pet_growth_result_t pet_companion_snapshot(pet_companion_view_t *out)
{
    if (!out) return PET_GROWTH_INVALID;
    if (!s.mutex || xSemaphoreTake(s.mutex,0)!=pdTRUE) return PET_GROWTH_NOT_READY;
    *out=(pet_companion_view_t){.state=s.model.companion,.eligible=pet_companion_eligible(&s.model),
        .themes=pet_companion_themes(&s.model),.writable=!s.store.write_uncertain && !s.waiting_for_time};
    xSemaphoreGive(s.mutex); return PET_GROWTH_OK;
}

pet_companion_result_t pet_companion_submit(const pet_companion_request_t *request)
{
    pet_companion_result_t result={PET_GROWTH_INVALID,false};
    if (!request || !request->request_id) return result;
    if (!s.mutex) { result.result=PET_GROWTH_NOT_READY; return result; }
    xSemaphoreTake(s.mutex,portMAX_DELAY);
    uint64_t now=now_ms();
    if (s.store.write_uncertain) result.result=PET_GROWTH_STORAGE_UNCERTAIN;
    else if ((now>=request->issued_at_ms?now-request->issued_at_ms:request->issued_at_ms-now)>300000)
        result.result=PET_GROWTH_STALE_REQUEST;
    else {
        result.result=advance_locked(now);
        if (result.result==PET_GROWTH_OK) {
            pet_growth_model_t candidate=s.model;
            result=pet_companion_apply(&candidate,request);
            if (result.result==PET_GROWTH_OK) {
                result.result=commit_locked(&candidate,now);
                if (result.result==PET_GROWTH_OK) publish_locked();
                else result.rewarded=false;
            }
        }
    }
    xSemaphoreGive(s.mutex); return result;
}
