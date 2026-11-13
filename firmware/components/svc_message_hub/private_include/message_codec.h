#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pet/message.h"

/* 消息信封编解码：纯 C、有界输入，可在主机测试中直接编译。 */

#define PET_MESSAGE_JSON_MAX 4096

/* 旧契约仅供历史样例验证；线上入口使用 nexus-core v1。 */
pet_message_error_t pet_message_parse(const char *json, size_t length,
                                      pet_message_view_t *out);

/* 生成 ACK JSON；返回写入长度，失败或截断返回 -1。 */
int pet_message_ack_format(char *out, size_t capacity, const char *message_id,
                           pet_message_ack_state_t state, uint32_t at_epoch_s,
                           uint8_t queue_depth);

pet_message_error_t pet_message_parse_nexus(const char *json, size_t length,
                                            const char *expected_device,
                                            pet_message_view_t *out);
int pet_message_nexus_ack_format(char *out, size_t capacity, const char *message_id,
                                 const pet_message_delivery_t *delivery,
                                 pet_message_ack_state_t state, bool expired);
