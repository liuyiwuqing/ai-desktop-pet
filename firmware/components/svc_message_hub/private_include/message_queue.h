#pragma once

#include <stdint.h>

#include "pet/message.h"

/* 有界优先级队列；存储由调用方提供，便于主机测试与 PSRAM 分配。 */
typedef struct {
    pet_message_view_t *slots;
    uint8_t capacity;
    uint8_t count;
    uint8_t dedupe_next;
    uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS];
    uint32_t dropped_total;
    uint32_t duplicate_total;
    uint32_t expired_total;
} pet_message_queue_t;

void pet_message_queue_init(pet_message_queue_t *queue, pet_message_view_t *slots,
                            uint8_t capacity);

/* 校验、去重、TTL 与入队；队满时按“最低优先级最旧”淘汰。 */
pet_message_error_t pet_message_queue_push(pet_message_queue_t *queue,
                                           const pet_message_view_t *message,
                                           uint32_t now_epoch_s);

/* 下一条待展示消息；顺序为优先级降序、创建时间升序。 */
const pet_message_view_t *pet_message_queue_next(const pet_message_queue_t *queue);
const pet_message_view_t *pet_message_queue_find(const pet_message_queue_t *queue, const char *id);

/* RECEIVED→DISPLAYED→READ；重复状态幂等，READ 后移出队列。 */
pet_message_error_t pet_message_queue_ack(pet_message_queue_t *queue, const char *id,
                                          pet_message_ack_state_t state);

/* 移除已过期消息，返回移除数量。 */
uint8_t pet_message_queue_purge_expired(pet_message_queue_t *queue, uint32_t now_epoch_s);

uint8_t pet_message_queue_count(const pet_message_queue_t *queue);
