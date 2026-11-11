#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "pet/message.h"

/* 消息中心对外入口：收包入队、队列快照与 ACK 发布。 */

/* ACK JSON 出口由组合根注入，避免消息中心直接依赖网络模块。 */
typedef esp_err_t (*pet_message_hub_ack_publisher_t)(const char *payload, size_t length,
                                                     void *context);

/* 返回当前 UTC 秒；时间未同步时返回 0，TTL 检查与 ACK 时间随之降级。 */
typedef uint32_t (*pet_message_hub_clock_t)(void);

esp_err_t pet_message_hub_init(pet_message_hub_ack_publisher_t publisher, void *context,
                               pet_message_hub_clock_t clock);
bool pet_message_hub_ready(void);

/* 解析 inbox 信封并入队；快照落盘成功后发布 received 事件并发送 ACK。
 * 未启用持久化或落盘失败时不发送 ACK，等待服务端重投后补发。 */
pet_message_error_t pet_message_hub_ingest(const char *json, size_t length);

/* 推进 ACK 状态并发布事件；READ 会把消息移出队列。 */
pet_message_error_t pet_message_hub_ack(const char *id, pet_message_ack_state_t state);

/* 清理过期消息并持久化；时间未同步（now=0）时不做处理。 */
uint8_t pet_message_hub_purge_expired(uint32_t now_epoch_s);

/* 按队列顺序复制快照，返回实际条数。 */
size_t pet_message_hub_snapshot(pet_message_view_t *out, size_t capacity);
bool pet_message_hub_get(const char *id, pet_message_view_t *out);
uint8_t pet_message_hub_depth(void);
