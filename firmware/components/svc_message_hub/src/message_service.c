#include "svc_message_hub/api.h"

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "pet/event_bus.h"
#include "message_codec.h"
#include "message_queue.h"
#include "message_store.h"

static const char *TAG = "svc_msg";
static pet_message_queue_t s_queue;
static pet_message_view_t *s_slots;
static pet_message_snapshot_t *s_work;
static pet_message_pending_ack_t s_acks[PET_MESSAGE_ACK_CAPACITY];
static uint8_t s_ack_count;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_flush_lock;
static pet_message_hub_ack_publisher_t s_publisher;
static void *s_publisher_context;
static pet_message_hub_clock_t s_clock;
static bool s_ready;
static uint8_t s_limit = PET_MESSAGE_QUEUE_CAPACITY;
static bool s_store_ready;
static char s_device_id[PET_NEXUS_DEVICE_ID_MAX + 1];
static char s_inbox_topic[80];

static uint32_t epoch_now(void) { return s_clock != NULL ? s_clock() : 0; }

static void publish_event(pet_event_id_t id, const pet_message_view_t *message,
                          pet_message_ack_state_t state, uint8_t depth)
{
    pet_message_event_t payload = {0};
    strlcpy(payload.id, message->id, sizeof(payload.id));
    payload.state = state;
    payload.queue_depth = depth;
    pet_event_t event;
    pet_event_prepare(&event, id, PET_EVENT_SOURCE_MESSAGE,
                      pet_message_event_priority(message->priority));
    event.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    memcpy(event.payload, &payload, sizeof(payload));
    event.payload_len = sizeof(payload);
    if (pet_event_bus_publish(&event) != ESP_OK)
        ESP_LOGW(TAG, "event %d dropped", (int)id);
}

/* 所有变更在工作副本中完成，NVS 提交成功后才替换可见状态。
 * 这把锁只包围本地快照事务，绝不持锁等待 MQTT 或 PUBACK。 */
static pet_message_queue_t begin_locked(void)
{
    memcpy(s_work->messages, s_slots, s_queue.count * sizeof(*s_slots));
    memcpy(s_work->acks, s_acks, sizeof(s_acks));
    s_work->ack_count = s_ack_count;
    pet_message_queue_t next = s_queue;
    next.slots = s_work->messages;
    return next;
}

static bool commit_locked(const pet_message_queue_t *next)
{
    if (!s_store_ready)
        return false;
    /* 重投只改变内存统计时不重写 Flash，已持久化的状态不依赖下一次写入成功。 */
    if (next->count == s_queue.count && next->dedupe_next == s_queue.dedupe_next &&
        s_work->ack_count == s_ack_count &&
        memcmp(next->slots, s_slots, next->count * sizeof(*s_slots)) == 0 &&
        memcmp(next->dedupe, s_queue.dedupe, sizeof(next->dedupe)) == 0 &&
        memcmp(s_work->acks, s_acks, s_ack_count * sizeof(s_acks[0])) == 0) {
        s_queue = *next;
        s_queue.slots = s_slots;
        return true;
    }
    s_work->count = next->count;
    memcpy(s_work->dedupe, next->dedupe, sizeof(next->dedupe));
    s_work->dedupe_next = next->dedupe_next;
    const esp_err_t err = pet_message_store_save(s_work);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "snapshot transaction failed: %s", esp_err_to_name(err));
        return false;
    }
    memcpy(s_slots, next->slots, next->count * sizeof(*s_slots));
    s_queue = *next;
    s_queue.slots = s_slots;
    memcpy(s_acks, s_work->acks, sizeof(s_acks));
    s_ack_count = s_work->ack_count;
    return true;
}

static int find_ack(const pet_message_pending_ack_t *acks, uint8_t count, const char *id)
{
    for (uint8_t i = 0; i < count; ++i)
        if (strcmp(acks[i].id, id) == 0)
            return i;
    return -1;
}

static bool same_delivery(const pet_message_delivery_t *left, const pet_message_delivery_t *right)
{
    return strcmp(left->event_id, right->event_id) == 0 &&
           strcmp(left->device_id, right->device_id) == 0;
}

static bool queue_ack_locked(const pet_message_view_t *message, pet_message_ack_state_t state,
                             uint32_t at, uint8_t depth, bool expired)
{
    /* 旧存档没有服务端投递身份，仅在本地推进显示/已读。 */
    if (message->delivery.event_id[0] == '\0')
        return true;
    int index = find_ack(s_work->acks, s_work->ack_count, message->id);
    if (index >= 0) {
        pet_message_pending_ack_t *ack = &s_work->acks[index];
        if (!same_delivery(&ack->delivery, &message->delivery))
            return false;
        if (ack->state >= state && (!expired || ack->expired)) {
            ack->confirmed = false;
            return true;
        }
    }
    if (index < 0) {
        if (s_work->ack_count == PET_MESSAGE_ACK_CAPACITY) {
            /* 只能淘汰已确认终态；未确认回执永不被新消息覆盖。 */
            unsigned victim = 0;
            while (victim < s_work->ack_count && !s_work->acks[victim].confirmed)
                ++victim;
            if (victim == s_work->ack_count)
                return false;
            memmove(&s_work->acks[victim], &s_work->acks[victim + 1],
                    (s_work->ack_count - victim - 1) * sizeof(s_work->acks[0]));
            --s_work->ack_count;
        }
        index = s_work->ack_count++;
    }
    pet_message_pending_ack_t *ack = &s_work->acks[index];
    memset(ack, 0, sizeof(*ack));
    strlcpy(ack->id, message->id, sizeof(ack->id));
    ack->delivery = message->delivery;
    ack->state = state;
    ack->at_epoch_s = at;
    ack->queue_depth = depth;
    ack->expired = expired;
    return true;
}

/* 过期删除与 rejected 回执使用同一事务；回执满时保留旧快照。 */
static bool purge_locked(pet_message_queue_t *next, uint32_t now, uint8_t *removed)
{
    *removed = 0;
    if (now == 0)
        return true;
    for (uint8_t i = 0; i < next->count;) {
        const pet_message_view_t *message = &next->slots[i];
        if (!pet_message_expired(message, now)) {
            ++i;
            continue;
        }
        if (!queue_ack_locked(message, PET_MESSAGE_STATE_READ, now, next->count - 1, true))
            return false;
        char id[PET_MESSAGE_ID_MAX + 1];
        memcpy(id, message->id, sizeof(id));
        if (pet_message_queue_ack(next, id, PET_MESSAGE_STATE_READ) != PET_MESSAGE_OK)
            return false;
        ++*removed;
    }
    return true;
}

esp_err_t pet_message_hub_init(const char *device_id, pet_message_hub_ack_publisher_t publisher, void *context,
                               pet_message_hub_clock_t clock)
{
    if (s_ready)
        return ESP_ERR_INVALID_STATE;
    if (!pet_nexus_device_id_valid(device_id))
        return ESP_ERR_INVALID_ARG;
    s_slots = heap_caps_malloc(PET_MESSAGE_QUEUE_CAPACITY * sizeof(*s_slots),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_slots == NULL)
        s_slots = heap_caps_malloc(PET_MESSAGE_QUEUE_CAPACITY * sizeof(*s_slots),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_work = heap_caps_malloc(sizeof(*s_work), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_work == NULL)
        s_work = heap_caps_malloc(sizeof(*s_work), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_lock = xSemaphoreCreateMutex();
    s_flush_lock = xSemaphoreCreateMutex();
    if (s_slots == NULL || s_work == NULL || s_lock == NULL || s_flush_lock == NULL) {
        heap_caps_free(s_slots);
        heap_caps_free(s_work);
        if (s_lock != NULL)
            vSemaphoreDelete(s_lock);
        if (s_flush_lock != NULL)
            vSemaphoreDelete(s_flush_lock);
        s_slots = NULL;
        s_work = NULL;
        s_lock = s_flush_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    memset(s_work, 0, sizeof(*s_work));
    strlcpy(s_device_id, device_id, sizeof(s_device_id));
    snprintf(s_inbox_topic, sizeof(s_inbox_topic), PET_NEXUS_TOPIC_PREFIX "%s/inbox", device_id);
    s_publisher = publisher;
    s_publisher_context = context;
    s_clock = clock;
    s_ack_count = 0;
    s_store_ready = pet_message_store_init() == ESP_OK;
    if (s_store_ready) {
        const esp_err_t err = pet_message_store_load(s_work);
        if (err == ESP_OK) {
            /* 暂不按 TTL 丢弃；上线后的清理会把过期回执一并持久化。 */
            pet_message_queue_restore(&s_queue, s_work->messages, s_work->count, s_work->dedupe,
                                      s_work->dedupe_next, 0);
            memcpy(s_acks, s_work->acks, sizeof(s_acks));
            s_ack_count = s_work->ack_count;
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "snapshot unavailable: %s", esp_err_to_name(err));
        }
        if (err != ESP_OK)
            memset(s_work, 0, sizeof(*s_work));
    }
    s_ready = true;
    ESP_LOGI(TAG, "inbox=%u pending_acks=%u storage=%d", s_queue.count, s_ack_count, s_store_ready);
    return ESP_OK;
}

bool pet_message_hub_ready(void)
{
    return s_ready;
}

pet_message_error_t pet_message_hub_ingest(const char *json, size_t length)
{
    if (!s_ready)
        return PET_MESSAGE_ERR_ARG;
    pet_message_view_t message;
    pet_message_error_t err = pet_message_parse_nexus(json, length, s_device_id, &message);
    if (err != PET_MESSAGE_OK)
        return err;
    const uint32_t now = epoch_now();
    if (now == 0)
        return PET_MESSAGE_ERR_TIME;
    uint8_t depth = 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pet_message_queue_t next = begin_locked();
    uint8_t purged;
    if (!purge_locked(&next, now, &purged)) {
        err = PET_MESSAGE_ERR_FULL;
        goto done;
    }
    const int pending = find_ack(s_work->acks, s_work->ack_count, message.id);
    const pet_message_view_t *queued = pet_message_queue_find(&next, message.id);
    if ((pending >= 0 && !same_delivery(&s_work->acks[pending].delivery, &message.delivery)) ||
        (queued != NULL && !same_delivery(&queued->delivery, &message.delivery))) {
        err = PET_MESSAGE_ERR_ID;
        goto done;
    }
    if (pending >= 0 && s_work->acks[pending].state == PET_MESSAGE_STATE_READ) {
        s_work->acks[pending].confirmed = false;
        err = PET_MESSAGE_ERR_DUPLICATE;
    } else if (queued != NULL) {
        err = PET_MESSAGE_ERR_DUPLICATE;
        if (!queue_ack_locked(queued, queued->state, now, next.count, false)) {
            err = PET_MESSAGE_ERR_FULL;
            goto done;
        }
    } else if (next.count >= s_limit && !pet_message_expired(&message, now)) {
        /* nexus-core 收到 received 后停止投递；已接受消息不能被优先级挤掉。 */
        err = PET_MESSAGE_ERR_FULL;
        goto done;
    } else {
        err = pet_message_queue_push(&next, &message, now);
        if (err == PET_MESSAGE_OK || err == PET_MESSAGE_ERR_EXPIRED) {
            const bool expired = err == PET_MESSAGE_ERR_EXPIRED;
            if (!queue_ack_locked(&message, expired ? PET_MESSAGE_STATE_READ : PET_MESSAGE_STATE_RECEIVED,
                                   now, next.count, expired)) {
                err = PET_MESSAGE_ERR_FULL;
                goto done;
            }
        } else if (purged == 0) {
            goto done;
        }
    }
    depth = next.count;
    if (!commit_locked(&next))
        err = PET_MESSAGE_ERR_DELIVERY;
done:
    xSemaphoreGive(s_lock);
    if (err == PET_MESSAGE_OK)
        publish_event(PET_EVT_MSG_RECEIVED, &message, PET_MESSAGE_STATE_RECEIVED, depth);
    return err;
}

pet_message_error_t pet_message_hub_receive(const char *topic, const char *json, size_t length,
                                            bool retained)
{
    if (!s_ready || retained || topic == NULL || strcmp(topic, s_inbox_topic) != 0)
        return PET_MESSAGE_ERR_ARG;
    return pet_message_hub_ingest(json, length);
}

pet_message_error_t pet_message_hub_ack(const char *id, pet_message_ack_state_t state)
{
    if (!s_ready || id == NULL)
        return PET_MESSAGE_ERR_ARG;
    pet_message_view_t message;
    pet_message_error_t err = PET_MESSAGE_ERR_ARG;
    uint8_t depth = 0;
    bool changed = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pet_message_queue_t next = begin_locked();
    const pet_message_view_t *found = pet_message_queue_find(&next, id);
    if (found == NULL)
        goto done;
    message = *found;
    if (message.state == state) {
        err = PET_MESSAGE_OK;
        goto done;
    }
    err = pet_message_queue_ack(&next, id, state);
    if (err != PET_MESSAGE_OK)
        goto done;
    const uint32_t now = epoch_now();
    if (!queue_ack_locked(&message, state, now != 0 ? now : message.created_at_epoch_s,
                          next.count, false)) {
        err = PET_MESSAGE_ERR_FULL;
        goto done;
    }
    if (!commit_locked(&next)) {
        err = PET_MESSAGE_ERR_DELIVERY;
        goto done;
    }
    changed = true;
    depth = next.count;
done:
    xSemaphoreGive(s_lock);
    if (changed) {
        const pet_event_id_t event =
            state == PET_MESSAGE_STATE_READ ? PET_EVT_MSG_READ : PET_EVT_MSG_DISPLAYED;
        publish_event(event, &message, state, depth);
    }
    return err;
}

uint8_t pet_message_hub_flush_acks(void)
{
    if (!s_ready || s_publisher == NULL)
        return 0;
    xSemaphoreTake(s_flush_lock, portMAX_DELAY);
    uint8_t confirmed = 0;
    for (uint8_t attempt = 0; attempt < PET_MESSAGE_ACK_CAPACITY; ++attempt) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        uint8_t candidate = 0;
        while (candidate < s_ack_count &&
               (s_acks[candidate].confirmed || strcmp(s_acks[candidate].delivery.device_id, s_device_id) != 0))
            ++candidate;
        if (candidate == s_ack_count) {
            xSemaphoreGive(s_lock);
            break;
        }
        const pet_message_pending_ack_t ack = s_acks[candidate];
        xSemaphoreGive(s_lock);
        char payload[320];
        const int length = pet_message_nexus_ack_format(payload, sizeof(payload), ack.id,
                                                        &ack.delivery, ack.state, ack.expired);
        if (length < 0 || s_publisher(payload, (size_t)length, s_publisher_context) != ESP_OK)
            break;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        pet_message_queue_t next = begin_locked();
        const int index = find_ack(s_work->acks, s_work->ack_count, ack.id);
        bool saved = true;
        /* 等待 PUBACK 时可能已升级为 read；旧 displayed 的确认不得清掉新回执。 */
        if (index >= 0 && s_work->acks[index].state == ack.state &&
            s_work->acks[index].expired == ack.expired &&
            same_delivery(&s_work->acks[index].delivery, &ack.delivery)) {
            if (ack.state == PET_MESSAGE_STATE_READ) {
                s_work->acks[index].confirmed = true;
            } else {
                memmove(&s_work->acks[index], &s_work->acks[index + 1],
                        (s_work->ack_count - index - 1) * sizeof(s_work->acks[0]));
                --s_work->ack_count;
            }
            saved = commit_locked(&next);
        }
        xSemaphoreGive(s_lock);
        if (!saved)
            break; /* Broker 已收但清理未落盘：保留，重启后安全重发。 */
        ++confirmed;
    }
    xSemaphoreGive(s_flush_lock);
    return confirmed;
}

uint8_t pet_message_hub_pending_acks(void)
{
    if (!s_ready)
        return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t count = 0;
    for (uint8_t i = 0; i < s_ack_count; ++i)
        if (!s_acks[i].confirmed)
            ++count;
    xSemaphoreGive(s_lock);
    return count;
}

uint8_t pet_message_hub_purge_expired(uint32_t now_epoch_s)
{
    if (!s_ready || now_epoch_s == 0)
        return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    pet_message_queue_t next = begin_locked();
    uint8_t removed;
    if (!purge_locked(&next, now_epoch_s, &removed) || (removed > 0 && !commit_locked(&next)))
        removed = 0;
    xSemaphoreGive(s_lock);
    return removed;
}

size_t pet_message_hub_snapshot(pet_message_view_t *out, size_t capacity)
{
    if (!s_ready || out == NULL || capacity == 0)
        return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const size_t count = capacity < s_queue.count ? capacity : s_queue.count;
    memcpy(out, s_slots, count * sizeof(*out));
    xSemaphoreGive(s_lock);
    return count;
}

bool pet_message_hub_get(const char *id, pet_message_view_t *out)
{
    if (!s_ready || id == NULL || out == NULL)
        return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const pet_message_view_t *found = pet_message_queue_find(&s_queue, id);
    if (found != NULL)
        *out = *found;
    xSemaphoreGive(s_lock);
    return found != NULL;
}

uint8_t pet_message_hub_depth(void)
{
    if (!s_ready)
        return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t count = s_queue.count;
    xSemaphoreGive(s_lock);
    return count;
}

esp_err_t pet_message_hub_set_limit(uint8_t capacity)
{
    if (!capacity || capacity>PET_MESSAGE_QUEUE_CAPACITY) return ESP_ERR_INVALID_ARG;
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock,portMAX_DELAY); s_limit=capacity; xSemaphoreGive(s_lock);
    return ESP_OK;
}
