/* Real message service/store/codec/storage with OS, flash and transport boundaries simulated. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/semphr.h"
#include "pet/event_bus.h"
#include "pet/storage.h"
#include "svc_message_hub/api.h"
#include "message_store.h"
#include "message_codec.h"
#include "net_policy.h"

static bool online = true;
static bool flash_writable = true;
static bool storage_available = true;
static unsigned erased;
static unsigned acks;
static char last_ack[320];
static bool upgrade_during_publish;
static bool broker_confirmed = true;
static const char *fail_key;
static size_t flash_budget;
static struct {
    char key[16];
    size_t length;
    unsigned char data[PET_MESSAGE_SNAPSHOT_MAX_SIZE];
} blobs[3];

esp_err_t nvs_flash_init(void) { return storage_available ? ESP_OK : ESP_ERR_NVS_NO_FREE_PAGES; }
esp_err_t nvs_flash_erase(void)
{
    ++erased;
    return ESP_OK;
}
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *out)
{
    (void)name;
    (void)mode;
    *out = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *out, size_t *length)
{
    (void)h;
    for (size_t i = 0; i < 3; ++i)
        if (strcmp(key, blobs[i].key) == 0) {
            if (*length < blobs[i].length)
                return ESP_ERR_INVALID_SIZE;
            *length = blobs[i].length;
            memcpy(out, blobs[i].data, *length);
            return ESP_OK;
        }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t length)
{
    (void)h;
    if (!flash_writable || (fail_key != NULL && strcmp(key, fail_key) == 0))
        return ESP_FAIL;
    if (flash_budget != 0) {
        size_t allocated = length;
        for (size_t i = 0; i < 3; ++i)
            allocated += blobs[i].length;
        if (allocated > flash_budget)
            return ESP_ERR_NO_MEM;
    }
    for (size_t i = 0; i < 3; ++i)
        if (blobs[i].key[0] == 0 || strcmp(key, blobs[i].key) == 0) {
            assert(length <= sizeof(blobs[i].data));
            strcpy(blobs[i].key, key);
            blobs[i].length = length;
            memcpy(blobs[i].data, data, length);
            return ESP_OK;
        }
    return ESP_ERR_NO_MEM;
}
esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *length)
{
    return nvs_get_blob(h, key, out, length);
}
esp_err_t nvs_erase_key(nvs_handle_t h, const char *key)
{
    (void)h;
    if (!flash_writable)
        return ESP_FAIL;
    for (size_t i = 0; i < 3; ++i) {
        if (strcmp(blobs[i].key, key) == 0) {
            memset(&blobs[i], 0, sizeof(blobs[i]));
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *data)
{
    return nvs_set_blob(h, key, data, strlen(data) + 1);
}
esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    return flash_writable ? ESP_OK : ESP_FAIL;
}
void *heap_caps_malloc(size_t n, unsigned caps)
{
    (void)caps;
    return malloc(n);
}
void heap_caps_free(void *p) { free(p); }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return calloc(1, 1); }
int xSemaphoreTake(SemaphoreHandle_t p, uint32_t timeout)
{
    (void)timeout;
    assert(*(char *)p == 0);
    *(char *)p = 1;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t p)
{
    assert(*(char *)p == 1);
    *(char *)p = 0;
    return 1;
}
void vSemaphoreDelete(SemaphoreHandle_t p) { free(p); }
int64_t esp_timer_get_time(void) { return 1000000; }
const char *esp_err_to_name(esp_err_t e)
{
    (void)e;
    return "test_error";
}
esp_err_t pet_event_bus_publish(const pet_event_t *event)
{
    (void)event;
    return ESP_OK;
}
static uint32_t now_epoch = 1794362400;
static uint32_t clock_now(void) { return now_epoch; }
static esp_err_t publish(const char *data, size_t length, void *context)
{
    (void)context;
    if (!online)
        return ESP_FAIL;
    assert(length < sizeof(last_ack));
    memcpy(last_ack, data, length);
    last_ack[length] = 0;
    ++acks;
    if (!broker_confirmed)
        return ESP_ERR_TIMEOUT;
    if (upgrade_during_publish) {
        upgrade_during_publish = false;
        assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
    }
    return ESP_OK;
}
static const char json[] =
    "{\"schemaVersion\":1,\"type\":\"inbox.message\","
    "\"eventId\":\"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee\",\"deviceId\":\"dodo-01\","
    "\"sentAt\":1794362400,\"expiresAt\":1794448800,\"payload\":{"
    "\"messageId\":\"11111111-2222-4333-8444-555555555555\",\"source\":\"halo\","
    "\"eventType\":\"comment.created\",\"priority\":\"normal\",\"title\":\"test\",\"summary\":\"hello\"}}";

int main(int argc, char **argv)
{
    if (argc > 3 && strcmp(argv[1], "wire-fixture") == 0) {
        FILE *input = fopen(argv[2], "rb");
        assert(input != NULL);
        char wire[1025];
        size_t length = fread(wire, 1, sizeof(wire), input);
        fclose(input);
        pet_message_view_t message;
        assert(pet_message_parse_nexus(wire, length, "dodo-01", &message) == PET_MESSAGE_OK);
        now_epoch = message.created_at_epoch_s;
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_receive("nexus/v1/devices/dodo-01/inbox", wire, length, false) == PET_MESSAGE_OK);
        FILE *output = fopen(argv[3], "wb");
        assert(output != NULL);
        assert(pet_message_hub_flush_acks() == 1);
        fprintf(output, "%s\n", last_ack);
        assert(pet_message_hub_ack(message.id, PET_MESSAGE_STATE_DISPLAYED) == PET_MESSAGE_OK);
        assert(pet_message_hub_flush_acks() == 1);
        fprintf(output, "%s\n", last_ack);
        assert(pet_message_hub_ack(message.id, PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
        assert(pet_message_hub_flush_acks() == 1);
        fprintf(output, "%s\n", last_ack);
        assert(pet_message_hub_ingest(wire, length) == PET_MESSAGE_ERR_DUPLICATE);
        assert(pet_message_hub_flush_acks() == 1);
        fprintf(output, "%s\n", last_ack);
        assert(pet_message_nexus_ack_format(last_ack, sizeof(last_ack), message.id, &message.delivery,
                                             PET_MESSAGE_STATE_READ, true) > 0);
        fprintf(output, "%s\n", last_ack);
        assert(pet_net_presence_format(last_ack, sizeof(last_ack), "dodo-01", true, now_epoch) > 0);
        fprintf(output, "%s\n", last_ack);
        assert(pet_net_presence_format(last_ack, sizeof(last_ack), "dodo-01", false, 0) > 0);
        fprintf(output, "%s\n", last_ack);
        assert(fclose(output) == 0);
        puts("message service: server-generated envelope -> persistent received/displayed/read/replay");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "snapshot-space") == 0) {
        static pet_message_snapshot_t snapshot;
        static pet_message_snapshot_t restored;
        flash_budget = 60000;
        assert(pet_message_store_init() == ESP_OK);
        /* 两代 v3 满快照约 46 KB，含旧回执留档；三份共存仍会超过 NVS 可用容量。 */
        snapshot.count = PET_MESSAGE_QUEUE_CAPACITY;
        snapshot.ack_count = PET_MESSAGE_ACK_CAPACITY;
        snapshot.legacy_ack_count = PET_MESSAGE_ACK_CAPACITY;
        for (unsigned i = 0; i < snapshot.count; ++i) {
            snapshot.messages[i].schema_version = 1;
            snprintf(snapshot.messages[i].id, sizeof(snapshot.messages[i].id), "msg_%07u", i);
        }
        for (unsigned i = 0; i < snapshot.ack_count; ++i) {
            snprintf(snapshot.acks[i].id, sizeof(snapshot.acks[i].id), "00000000-0000-4000-8000-%012u", i);
            strcpy(snapshot.acks[i].delivery.event_id, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
            strcpy(snapshot.acks[i].delivery.device_id, "dodo-01");
            snprintf(snapshot.legacy_acks[i].id, sizeof(snapshot.legacy_acks[i].id), "msg_%07u", i);
            snapshot.acks[i].state = PET_MESSAGE_STATE_READ;
        }
        for (unsigned i = 0; i < 10; ++i) {
            snapshot.messages[0].created_at_epoch_s = 100 + i;
            assert(pet_message_store_save(&snapshot) == ESP_OK);
        }
        /* active 指针写失败后，重试仍只能覆盖未提交代。 */
        fail_key = "active";
        snapshot.messages[0].created_at_epoch_s = 999;
        assert(pet_message_store_save(&snapshot) != ESP_OK);
        fail_key = "snap0";
        assert(pet_message_store_save(&snapshot) != ESP_OK);
        fail_key = NULL;
        assert(pet_message_store_load(&restored) == ESP_OK);
        assert(restored.messages[0].created_at_epoch_s == 109);
        snapshot.messages[0].created_at_epoch_s = 108;
        assert(pet_message_store_save(&snapshot) == ESP_OK);
        snapshot.messages[0].created_at_epoch_s = 109;
        assert(pet_message_store_save(&snapshot) == ESP_OK);
        unsigned active = 0;
        for (size_t i = 0; i < 3; ++i)
            if (strcmp(blobs[i].key, "active") == 0)
                active = blobs[i].data[0];
        for (size_t i = 0; i < 3; ++i)
            if (strcmp(blobs[i].key, active == 0 ? "snap0" : "snap1") == 0)
                blobs[i].data[0] ^= 0xFF;
        assert(pet_message_store_load(&restored) == ESP_OK);
        assert(restored.messages[0].created_at_epoch_s == 108);
        fail_key = active == 0 ? "snap0" : "snap1";
        assert(pet_message_store_save(&snapshot) != ESP_OK);
        fail_key = NULL;
        assert(pet_message_store_load(&restored) == ESP_OK);
        assert(restored.messages[0].created_at_epoch_s == 108);
        assert(pet_message_store_save(&snapshot) == ESP_OK);
        assert(pet_message_store_load(&restored) == ESP_OK);
        assert(restored.messages[0].created_at_epoch_s == 109);
        puts("message store: bounded peak space and failed replacement preserve valid generation");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "legacy-migration") == 0) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/message-snapshot-v2.bin", TEST_FIXTURE_DIR);
        FILE *file = fopen(path, "rb");
        assert(file != NULL);
        strcpy(blobs[0].key, "snap0");
        blobs[0].length = fread(blobs[0].data, 1, sizeof(blobs[0].data), file);
        fclose(file);
        strcpy(blobs[1].key, "active");
        blobs[1].length = 1;
        blobs[1].data[0] = 0;
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        pet_message_view_t legacy;
        assert(pet_message_hub_get("msg_legacy1", &legacy));
        assert(pet_message_hub_pending_acks() == 0 && pet_message_hub_flush_acks() == 0);
        assert(pet_message_hub_ack("msg_legacy1", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_OK);
        static pet_message_snapshot_t restored;
        assert(pet_message_store_load(&restored) == ESP_OK);
        assert(restored.legacy_ack_count == 1 && strcmp(restored.legacy_acks[0].id, "msg_oldread") == 0);
        assert(restored.ack_count == 1 && restored.count == 1);
        assert(pet_message_hub_flush_acks() == 1 && acks == 1);
        assert(strstr(last_ack, "msg_") == NULL);
        puts("message service: v2 migration preserves old messages/receipts and accepts new delivery");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "identity") == 0) {
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_receive("nexus/v1/devices/dodo-02/inbox", json, strlen(json), false) != PET_MESSAGE_OK);
        assert(pet_message_hub_receive("evil/nexus/v1/devices/dodo-01/inbox", json, strlen(json), false) != PET_MESSAGE_OK);
        assert(pet_message_hub_receive("nexus/v1/devices/dodo-01/inbox", json, strlen(json), true) != PET_MESSAGE_OK);
        char wrong[sizeof(json)];
        strcpy(wrong, json);
        memcpy(strstr(wrong, "dodo-01"), "dodo-02", 7);
        assert(pet_message_hub_ingest(wrong, strlen(wrong)) != PET_MESSAGE_OK);
        assert(pet_message_hub_depth() == 0 && pet_message_hub_pending_acks() == 0);
        now_epoch = 0;
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_TIME);
        now_epoch = 1794362400;
        assert(pet_message_hub_receive("nexus/v1/devices/dodo-01/inbox", json, strlen(json), false) == PET_MESSAGE_OK);
        strcpy(wrong, json);
        strstr(wrong, "aaaaaaaa-bbbb")[0] = 'b';
        assert(pet_message_hub_ingest(wrong, strlen(wrong)) == PET_MESSAGE_ERR_ID);
        assert(pet_message_hub_flush_acks() == 1);
        assert(strstr(last_ack, "aaaaaaaa-bbbb") != NULL);
        now_epoch = 1794448800;
        assert(pet_message_hub_purge_expired(now_epoch) == 1);
        assert(pet_message_hub_flush_acks() == 1);
        assert(strstr(last_ack, "\"status\":\"rejected\",\"reason\":\"expired\"") != NULL);
        puts("message service: exact topic/device, non-retained input, synced time and expiry semantics");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "limit") == 0) {
        assert(pet_message_hub_init("dodo-01",publish,NULL,clock_now)==ESP_OK);
        char message[sizeof(json)+20],ids[3][37];
        for(unsigned i=0;i<3;++i) {
            strcpy(message,json);snprintf(ids[i],37,"00000000-0000-4000-8000-%012u",i);
            memcpy(strstr(message,"11111111-2222-4333-8444-555555555555"),ids[i],36);
            assert(pet_message_hub_ingest(message,strlen(message))==PET_MESSAGE_OK);
            assert(pet_message_hub_flush_acks()==1);
        }
        assert(pet_message_hub_set_limit(0)==ESP_ERR_INVALID_ARG);
        assert(pet_message_hub_set_limit(2)==ESP_OK&&pet_message_hub_depth()==3);
        for(unsigned i=0;i<3;++i){pet_message_view_t kept;assert(pet_message_hub_get(ids[i],&kept));}
        assert(pet_message_hub_ingest(json,strlen(json))==PET_MESSAGE_ERR_FULL);
        assert(pet_message_hub_ack(ids[0],PET_MESSAGE_STATE_READ)==PET_MESSAGE_OK);
        assert(pet_message_hub_ingest(json,strlen(json))==PET_MESSAGE_ERR_FULL);
        assert(pet_message_hub_ack(ids[1],PET_MESSAGE_STATE_READ)==PET_MESSAGE_OK);
        assert(pet_message_hub_ingest(json,strlen(json))==PET_MESSAGE_OK&&pet_message_hub_depth()==2);
        puts("message service: shrinking limit preserves accepted messages and only gates new delivery");return 0;
    }
    if (argc > 1 && strcmp(argv[1], "capacity") == 0) {
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        online = false;
        char message[sizeof(json) + 20];
        for (unsigned i = 0; i < 16; ++i) {
            strcpy(message, json);
            char *id = strstr(message, "11111111-2222-4333-8444-555555555555");
            char unique[37];
            snprintf(unique, sizeof(unique), "00000000-0000-4000-8000-%012u", i);
            memcpy(id, unique, 36);
            assert(pet_message_hub_ingest(message, strlen(message)) == PET_MESSAGE_OK);
            assert(pet_message_hub_ack(unique, PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
        }
        assert(pet_message_hub_pending_acks() == 16 && pet_message_hub_depth() == 0);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_FULL);
        assert(pet_message_hub_pending_acks() == 16 && pet_message_hub_depth() == 0);
        online = true;
        assert(pet_message_hub_flush_acks() == 16);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_OK);
        puts("message service: receipt saturation applies backpressure without losing records");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "inbox-capacity") == 0) {
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        char message[sizeof(json) + 20];
        for (unsigned i = 0; i < 8; ++i) {
            strcpy(message, json);
            char unique[37];
            snprintf(unique, sizeof(unique), "00000000-0000-4000-8000-%012u", i);
            memcpy(strstr(message, "11111111-2222-4333-8444-555555555555"), unique, 36);
            assert(pet_message_hub_ingest(message, strlen(message)) == PET_MESSAGE_OK);
            assert(pet_message_hub_flush_acks() == 1);
        }
        assert(pet_message_hub_depth() == 8);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_FULL);
        pet_message_view_t first;
        assert(pet_message_hub_get("00000000-0000-4000-8000-000000000000", &first));
        assert(pet_message_hub_ack(first.id, PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_OK);
        puts("message service: full inbox never evicts an acknowledged delivery, retry succeeds after read");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "concurrent-upgrade") == 0) {
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_OK);
        assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_DISPLAYED) == PET_MESSAGE_OK);
        upgrade_during_publish = true;
        assert(pet_message_hub_flush_acks() == 2);
        assert(pet_message_hub_depth() == 0 && pet_message_hub_pending_acks() == 0);
        assert(strstr(last_ack, "\"status\":\"read\"") != NULL);
        puts("message service: late displayed confirmation cannot delete newer read receipt");
        return 0;
    }
    if (argc > 2 && strcmp(argv[1], "restore-read") == 0) {
        FILE *file = fopen(argv[2], "rb");
        assert(file != NULL && fread(blobs, sizeof(blobs), 1, file) == 1);
        fclose(file);
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_depth() == 0 && pet_message_hub_pending_acks() == 1);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DUPLICATE);
        assert(pet_message_hub_flush_acks() == 1);
        assert(strstr(last_ack, "\"status\":\"read\"") != NULL);
        assert(pet_message_hub_pending_acks() == 0);
        puts("message service: reboot restores pending read without restoring unread");
        return 0;
    }
    if (argc > 2 && (strcmp(argv[1], "restore-confirmed") == 0 || strcmp(argv[1], "restore-other-device") == 0)) {
        const bool changed_device = strcmp(argv[1], "restore-other-device") == 0;
        FILE *file = fopen(argv[2], "rb");
        assert(file != NULL && fread(blobs, sizeof(blobs), 1, file) == 1);
        fclose(file);
        assert(pet_message_hub_init(changed_device ? "dodo-02" : "dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_depth() == 0);
        assert(pet_message_hub_pending_acks() == (changed_device ? 1 : 0));
        assert(pet_message_hub_flush_acks() == 0 && acks == 0);
        if (!changed_device) {
            assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DUPLICATE);
            assert(pet_message_hub_pending_acks() == 1 && pet_message_hub_flush_acks() == 1);
            assert(strstr(last_ack, "\"status\":\"read\"") != NULL);
        }
        puts(changed_device ? "message service: identity change cannot send previous-device receipts"
                            : "message service: confirmed read survives reboot and acknowledges server replay");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "storage-failed") == 0) {
        storage_available = false;
        assert(pet_storage_init() == ESP_ERR_NVS_NO_FREE_PAGES);
        assert(erased == 0);
        assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
        assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DELIVERY);
        assert(acks == 0);
        assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_READ) != PET_MESSAGE_OK);
        assert(pet_message_hub_depth() == 0);
        puts("message service: unavailable storage does not erase or acknowledge");
        return 0;
    }
    assert(pet_message_hub_init("dodo-01", publish, NULL, clock_now) == ESP_OK);
    flash_writable = false;
    assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DELIVERY);
    assert(acks == 0 && pet_message_hub_depth() == 0);
    flash_writable = true;
    assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_OK);
    assert(acks == 0 && pet_message_hub_pending_acks() == 1);
    flash_writable = false;
    assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DUPLICATE);
    flash_writable = true;
    assert(pet_message_hub_flush_acks() == 1);
    assert(acks == 1 && strstr(last_ack, "received") != NULL);
    assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_DISPLAYED) == PET_MESSAGE_OK);
    assert(pet_message_hub_flush_acks() == 1);
    assert(acks == 2 && strstr(last_ack, "displayed") != NULL);
    assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DUPLICATE);
    fail_key = "active";
    assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_READ) == PET_MESSAGE_ERR_DELIVERY);
    assert(pet_message_hub_depth() == 1);
    fail_key = NULL;
    flash_writable = false;
    assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_READ) != PET_MESSAGE_OK);
    assert(pet_message_hub_depth() == 1);
    flash_writable = true;
    online = false;
    assert(pet_message_hub_ack("11111111-2222-4333-8444-555555555555", PET_MESSAGE_STATE_READ) == PET_MESSAGE_OK);
    assert(pet_message_hub_depth() == 0 && pet_message_hub_pending_acks() == 1);
    assert(pet_message_hub_flush_acks() == 0);
    if (argc > 2 && strcmp(argv[1], "save-read") == 0) {
        FILE *file = fopen(argv[2], "wb");
        assert(file != NULL && fwrite(blobs, sizeof(blobs), 1, file) == 1);
        fclose(file);
        return 0;
    }
    online = true;
    broker_confirmed = false;
    assert(pet_message_hub_flush_acks() == 0 && pet_message_hub_pending_acks() == 1);
    broker_confirmed = true;
    flash_writable = false;
    assert(pet_message_hub_flush_acks() == 0 && pet_message_hub_pending_acks() == 1);
    flash_writable = true;
    assert(pet_message_hub_flush_acks() == 1);
    assert(pet_message_hub_pending_acks() == 0 && strstr(last_ack, "read") != NULL);
    assert(pet_message_hub_ingest(json, strlen(json)) == PET_MESSAGE_ERR_DUPLICATE);
    assert(pet_message_hub_pending_acks() == 1 && pet_message_hub_depth() == 0);
    assert(pet_message_hub_flush_acks() == 1 && strstr(last_ack, "\"status\":\"read\"") != NULL);
    assert(strstr(last_ack, "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee") != NULL);
    if (argc > 2 && strcmp(argv[1], "save-confirmed") == 0) {
        FILE *file = fopen(argv[2], "wb");
        assert(file != NULL && fwrite(blobs, sizeof(blobs), 1, file) == 1);
        fclose(file);
    }
    puts("message service: atomic inbox/receipt, offline read, replay after confirmation");
    return 0;
}
