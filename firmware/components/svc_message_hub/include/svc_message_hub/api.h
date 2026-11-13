#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "pet/message.h"

/* 消息中心对外入口：收包入队、队列快照与 ACK 发布。 */

/* ACK JSON 出口由组合根注入，避免消息中心直接依赖网络模块。
 * 只由独立补发任务调用；ESP_OK 必须表示 Broker 已确认（PUBACK），不能仅表示入队。
 * 可有界等待网络；调用期间不持消息锁。 */
typedef esp_err_t (*pet_message_hub_ack_publisher_t)(const char *payload, size_t length,
                                                     void *context);

/* 返回当前 UTC 秒；时间未同步时返回 0，暂不接受新投递，等待服务端重试。 */
typedef uint32_t (*pet_message_hub_clock_t)(void);

esp_err_t pet_message_hub_init(const char *device_id, pet_message_hub_ack_publisher_t publisher, void *context,
                               pet_message_hub_clock_t clock);
bool pet_message_hub_ready(void);

/* 收件箱和 received 回执原子落盘后才入队、发布事件；失败不接受消息。 */
pet_message_error_t pet_message_hub_ingest(const char *json, size_t length);
/* 网络入口还校验精确主题并拒绝 retained 收件；配置主题没有收件副作用。 */
pet_message_error_t pet_message_hub_receive(const char *topic, const char *json, size_t length,
                                            bool retained);

/* 状态和待发回执原子落盘后推进；READ 移出队列。离线可确认，落盘失败保留原状态。 */
pet_message_error_t pet_message_hub_ack(const char *id, pet_message_ack_state_t state);

/* 从独立任务调用：补发至 Broker 确认，失败保留到下次/重启；不从 UI/MQTT 回调调用。 */
uint8_t pet_message_hub_flush_acks(void);
uint8_t pet_message_hub_pending_acks(void);

/* 清理过期消息并持久化 rejected/expired；时间未同步（now=0）时不做处理。 */
uint8_t pet_message_hub_purge_expired(uint32_t now_epoch_s);

/* 按队列顺序复制快照，返回实际条数。 */
size_t pet_message_hub_snapshot(pet_message_view_t *out, size_t capacity);
bool pet_message_hub_get(const char *id, pet_message_view_t *out);
uint8_t pet_message_hub_depth(void);

/* 仅限制新投递；缩小后保留所有已接受消息，直到自然确认/过期。 */
esp_err_t pet_message_hub_set_limit(uint8_t capacity);
