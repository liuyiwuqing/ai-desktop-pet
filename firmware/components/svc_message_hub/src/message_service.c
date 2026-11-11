#include "svc_message_hub/api.h"

#include <string.h>

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
static pet_message_snapshot_t *s_persist;
static SemaphoreHandle_t s_lock;
static SemaphoreHandle_t s_persist_lock;
static pet_message_hub_ack_publisher_t s_publisher;
static void *s_publisher_context;
static pet_message_hub_clock_t s_clock;
static bool s_ready;
static bool s_store_ready;
static bool s_dirty;

static uint32_t epoch_now(void)
{
    return s_clock != NULL ? s_clock() : 0;
}

static void publish_ack(const char *id, pet_message_ack_state_t state, uint32_t at_epoch_s,
                        uint8_t depth)
{
    if (s_publisher == NULL) {
        return;
    }

    char payload[160];
    const int length =
        pet_message_ack_format(payload, sizeof(payload), id, state, at_epoch_s, depth);
    if (length < 0) {
        ESP_LOGW(TAG, "ack format failed: %s", id);
        return;
    }

    const esp_err_t err = s_publisher(payload, (size_t)length, s_publisher_context);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ack publish failed: %s", esp_err_to_name(err));
    }
}

static void publish_event(pet_event_id_t id, const pet_message_view_t *message,
                          pet_message_ack_state_t state, uint8_t depth)
{
    pet_message_event_t payload;
    memset(&payload, 0, sizeof(payload));
    strlcpy(payload.id, message->id, sizeof(payload.id));
    payload.state = state;
    payload.queue_depth = depth;

    pet_event_t event;
    pet_event_prepare(&event, id, PET_EVENT_SOURCE_MESSAGE,
                      pet_message_event_priority(message->priority));
    event.timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    memcpy(event.payload, &payload, sizeof(payload));
    event.payload_len = (uint16_t)sizeof(payload);

    if (pet_event_bus_publish(&event) != ESP_OK) {
        ESP_LOGW(TAG, "event %d dropped", (int)id);
    }
}

/* 复制队列到导出缓冲区后写 NVS，避免持锁做慢速 I/O。
 * 未启用持久化时直接返回 true，退回 S5-C1 的立即 ACK 行为。 */
static bool persist(void)
{
    if (!s_store_ready) {
        return true;
    }
    if (xSemaphoreTake(s_persist_lock, portMAX_DELAY) != pdTRUE) {
        return false;
    }

    bool saved = true;
    for (;;) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!s_dirty) {
            xSemaphoreGive(s_lock);
            break;
        }
        s_persist->count = s_queue.count;
        s_persist->dedupe_next = s_queue.dedupe_next;
        if (s_queue.count > 0) {
            memcpy(s_persist->messages, s_queue.slots,
                   (size_t)s_queue.count * sizeof(s_queue.slots[0]));
        }
        memcpy(s_persist->dedupe, s_queue.dedupe, sizeof(s_persist->dedupe));
        s_dirty = false;
        xSemaphoreGive(s_lock);

        const esp_err_t err = pet_message_store_save(s_persist);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "snapshot save failed: %s", esp_err_to_name(err));
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_dirty = true;
            xSemaphoreGive(s_lock);
            saved = false;
            break;
        }
    }

    xSemaphoreGive(s_persist_lock);
    return saved;
}

static void restore_from_store(void)
{
    esp_err_t err = pet_message_store_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "persistence disabled: %s", esp_err_to_name(err));
        return;
    }
    s_store_ready = true;

    err = pet_message_store_load(s_persist);
    if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "no persisted messages");
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "snapshot unusable: %s", esp_err_to_name(err));
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t restored = pet_message_queue_restore(
        &s_queue, s_persist->messages, s_persist->count, s_persist->dedupe,
        s_persist->dedupe_next, epoch_now());
    const uint8_t loaded = s_persist->count;
    if (restored != loaded) {
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "restored %u/%u messages", restored, loaded);
    if (restored != loaded) {
        persist();
    }
}

esp_err_t pet_message_hub_init(pet_message_hub_ack_publisher_t publisher, void *context,
                               pet_message_hub_clock_t clock)
{
    if (s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    s_slots = heap_caps_malloc((size_t)PET_MESSAGE_QUEUE_CAPACITY * sizeof(*s_slots),
                               MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_slots == NULL) {
        s_slots = heap_caps_malloc((size_t)PET_MESSAGE_QUEUE_CAPACITY * sizeof(*s_slots),
                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_slots == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_persist = heap_caps_malloc(sizeof(*s_persist), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_persist == NULL) {
        s_persist = heap_caps_malloc(sizeof(*s_persist), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (s_persist == NULL) {
        ESP_LOGW(TAG, "persist buffer unavailable; running memory-only");
    }

    s_lock = xSemaphoreCreateMutex();
    s_persist_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL || (s_persist != NULL && s_persist_lock == NULL)) {
        if (s_persist_lock != NULL) {
            vSemaphoreDelete(s_persist_lock);
        }
        if (s_lock != NULL) {
            vSemaphoreDelete(s_lock);
        }
        heap_caps_free(s_persist);
        s_persist = NULL;
        heap_caps_free(s_slots);
        s_slots = NULL;
        return ESP_ERR_NO_MEM;
    }

    pet_message_queue_init(&s_queue, s_slots, PET_MESSAGE_QUEUE_CAPACITY);
    s_publisher = publisher;
    s_publisher_context = context;
    s_clock = clock;
    s_ready = true;

    ESP_LOGI(TAG, "queue=%u x %u B", PET_MESSAGE_QUEUE_CAPACITY,
             (unsigned)sizeof(*s_slots));

    if (s_persist != NULL) {
        restore_from_store();
    } else {
        s_store_ready = false;
    }
    return ESP_OK;
}

bool pet_message_hub_ready(void)
{
    return s_ready;
}

pet_message_error_t pet_message_hub_ingest(const char *json, size_t length)
{
    if (!s_ready) {
        return PET_MESSAGE_ERR_ARG;
    }

    pet_message_view_t message;
    pet_message_error_t err = pet_message_parse(json, length, &message);
    if (err != PET_MESSAGE_OK) {
        return err;
    }

    const uint32_t now = epoch_now();
    uint8_t depth = 0;
    uint8_t purged = 0;
    bool still_queued = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (now != 0) {
        purged = pet_message_queue_purge_expired(&s_queue, now);
    }
    err = pet_message_queue_push(&s_queue, &message, now);
    if (err == PET_MESSAGE_OK) {
        depth = pet_message_queue_count(&s_queue);
        s_dirty = true;
    } else if (err == PET_MESSAGE_ERR_DUPLICATE) {
        still_queued = pet_message_queue_find(&s_queue, message.id) != NULL;
    }
    if (purged > 0) {
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);

    if (err == PET_MESSAGE_ERR_EXPIRED) {
        /* 过期消息不展示；回 read 防止服务端在保留窗口内反复重投。 */
        if (purged > 0) {
            persist();
        }
        publish_ack(message.id, PET_MESSAGE_STATE_READ, message.created_at_epoch_s, 0);
        return err;
    }
    if (err == PET_MESSAGE_ERR_DUPLICATE) {
        /* 服务端重投说明上次 received 可能丢失；仍在队列中才补发，避免状态回退。 */
        if (still_queued && persist()) {
            publish_ack(message.id, PET_MESSAGE_STATE_RECEIVED,
                        now != 0 ? now : message.created_at_epoch_s,
                        pet_message_hub_depth());
        } else if (purged > 0) {
            persist();
        }
        return err;
    }
    if (err != PET_MESSAGE_OK) {
        if (purged > 0) {
            persist();
        }
        return err;
    }

    if (!persist()) {
        /* 已进入内存队列但未落盘：不 ACK，服务端重投时会再次触发保存与补发。 */
        publish_event(PET_EVT_MSG_RECEIVED, &message, PET_MESSAGE_STATE_RECEIVED, depth);
        ESP_LOGW(TAG, "queued %s without durable snapshot; ACK deferred", message.id);
        return PET_MESSAGE_OK;
    }

    publish_ack(message.id, PET_MESSAGE_STATE_RECEIVED,
                now != 0 ? now : message.created_at_epoch_s, depth);
    publish_event(PET_EVT_MSG_RECEIVED, &message, PET_MESSAGE_STATE_RECEIVED, depth);
    ESP_LOGI(TAG, "queued %s priority=%d depth=%u", message.id, (int)message.priority, depth);
    return PET_MESSAGE_OK;
}

pet_message_error_t pet_message_hub_ack(const char *id, pet_message_ack_state_t state)
{
    if (!s_ready || id == NULL) {
        return PET_MESSAGE_ERR_ARG;
    }

    const uint32_t now = epoch_now();
    pet_message_view_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    pet_message_error_t err = PET_MESSAGE_ERR_ARG;
    bool changed = false;
    uint8_t depth = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const pet_message_view_t *found = pet_message_queue_find(&s_queue, id);
    if (found != NULL) {
        snapshot = *found;
        depth = pet_message_queue_count(&s_queue);
        if (snapshot.state == state) {
            err = PET_MESSAGE_OK;
        } else {
            err = pet_message_queue_ack(&s_queue, id, state);
            changed = err == PET_MESSAGE_OK;
            if (changed) {
                depth = pet_message_queue_count(&s_queue);
                s_dirty = true;
            }
        }
    }
    xSemaphoreGive(s_lock);

    if (err != PET_MESSAGE_OK || !changed) {
        return err;
    }

    /* 展示进度优先推进；落盘失败时保留 dirty，由后续变更重试。 */
    persist();
    publish_ack(id, state, now != 0 ? now : snapshot.created_at_epoch_s, depth);

    pet_event_id_t event_id = PET_EVT_MSG_DISPLAYED;
    if (state == PET_MESSAGE_STATE_RECEIVED) {
        event_id = PET_EVT_MSG_RECEIVED;
    } else if (state == PET_MESSAGE_STATE_READ) {
        event_id = PET_EVT_MSG_READ;
    }
    publish_event(event_id, &snapshot, state, depth);
    return PET_MESSAGE_OK;
}

uint8_t pet_message_hub_purge_expired(uint32_t now_epoch_s)
{
    if (!s_ready || now_epoch_s == 0) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t removed = pet_message_queue_purge_expired(&s_queue, now_epoch_s);
    if (removed > 0) {
        s_dirty = true;
    }
    xSemaphoreGive(s_lock);

    if (removed > 0) {
        persist();
        ESP_LOGI(TAG, "purged %u expired messages", removed);
    }
    return removed;
}

size_t pet_message_hub_snapshot(pet_message_view_t *out, size_t capacity)
{
    if (!s_ready || out == NULL || capacity == 0) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t count = 0;
    for (uint8_t i = 0; i < s_queue.count && count < capacity; i++) {
        out[count++] = s_queue.slots[i];
    }
    xSemaphoreGive(s_lock);
    return count;
}

bool pet_message_hub_get(const char *id, pet_message_view_t *out)
{
    if (!s_ready || id == NULL || out == NULL) {
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const pet_message_view_t *found = pet_message_queue_find(&s_queue, id);
    const bool found_ok = found != NULL;
    if (found_ok) {
        *out = *found;
    }
    xSemaphoreGive(s_lock);
    return found_ok;
}

uint8_t pet_message_hub_depth(void)
{
    if (!s_ready) {
        return 0;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    const uint8_t depth = pet_message_queue_count(&s_queue);
    xSemaphoreGive(s_lock);
    return depth;
}
