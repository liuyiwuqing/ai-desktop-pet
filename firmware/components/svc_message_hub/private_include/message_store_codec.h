#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pet/message.h"

/* 快照序列化：纯 C、固定小端布局，可在主机测试中直接编译。
 * 布局：header(20 B) + 去重环(16 × 8 B) + count × record(固定长度)。
 * header 含 record_size 与 crc32，结构体布局变化或数据损坏时整代丢弃。 */

#define PET_MESSAGE_STORE_MAGIC 0x31534D50u /* "PMS1" */
#define PET_MESSAGE_STORE_LAYOUT_VERSION 1u
#define PET_MESSAGE_STORE_HEADER_SIZE 20u

#define PET_MESSAGE_STORE_RECORD_SIZE                                                  \
    (8u + 12u + (PET_MESSAGE_ID_MAX + 1u) + (PET_MESSAGE_SOURCE_MAX + 1u) +            \
     (PET_MESSAGE_DEDUPE_KEY_MAX + 1u) + (PET_MESSAGE_TITLE_MAX + 1u) +                \
     (PET_MESSAGE_BODY_MAX + 1u))

#define PET_MESSAGE_STORE_MAX_SIZE                                                     \
    (PET_MESSAGE_STORE_HEADER_SIZE +                                                    \
     (size_t)PET_MESSAGE_DEDUPE_SLOTS * 8u +                                            \
     (size_t)PET_MESSAGE_QUEUE_CAPACITY * PET_MESSAGE_STORE_RECORD_SIZE)

typedef enum {
    PET_MESSAGE_STORE_OK = 0,
    PET_MESSAGE_STORE_ERR_ARG,
    PET_MESSAGE_STORE_ERR_SIZE,
    PET_MESSAGE_STORE_ERR_MAGIC,
    PET_MESSAGE_STORE_ERR_VERSION,
    PET_MESSAGE_STORE_ERR_RECORD,
    PET_MESSAGE_STORE_ERR_CRC,
} pet_message_store_error_t;

size_t pet_message_store_record_size(void);
size_t pet_message_store_encoded_size(uint8_t count);

/* 编码成功返回 OK 并写入实际长度；容量不足返回 ERR_SIZE。 */
pet_message_store_error_t pet_message_store_encode(
    const pet_message_view_t *messages, uint8_t count,
    const uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS], uint8_t dedupe_next,
    uint8_t *out, size_t capacity, size_t *out_length);

/* 解码并校验；容量不足或记录非法时不写入调用方缓冲区之外。 */
pet_message_store_error_t pet_message_store_decode(
    const uint8_t *data, size_t length,
    pet_message_view_t *messages, uint8_t capacity,
    uint8_t *count, uint64_t dedupe[PET_MESSAGE_DEDUPE_SLOTS],
    uint8_t *dedupe_next);
