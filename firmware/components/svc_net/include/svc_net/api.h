#pragma once

#include <stdbool.h>

#include "esp_err.h"

#include "pet/hal/net.h"
#include "pet/net.h"

/* 网络服务：配置校验、驱动选择与生命周期；真实/假传输由 Kconfig 决定。 */

esp_err_t pet_net_init(const pet_net_config_t *config);
esp_err_t pet_net_start(void);
esp_err_t pet_net_stop(void);
bool pet_net_is_online(void);

/* 注册收包回调；应在 pet_net_start 前调用。 */
esp_err_t pet_net_set_rx_handler(pet_net_rx_handler_t handler, void *context);

/* 使用已保存的 device_id 构建主题并发布。 */
esp_err_t pet_net_publish_topic(pet_net_topic_t topic, const char *payload, size_t length,
                                bool retain);

/* 后台任务使用：等待 Broker PUBACK；超时/断线不等于应用回执已经送达。 */
esp_err_t pet_net_publish_confirmed(pet_net_topic_t topic, const char *payload, size_t length,
                                    bool retain, uint32_t timeout_ms);

/* SNTP 是否已成功对时；未对时时暂不接受有 TTL 的新消息。 */
bool pet_net_is_time_synced(void);

/* 只读链路信息，仅供遥测上报；不写入日志，也不代表 Broker 已连接或消息已送达。
   IPv4 为点分十进制；未关联或未取得地址时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pet_net_local_ip(char *out, size_t capacity);

/* 当前 AP 的 RSSI（dBm，<=0）；未关联或取不到时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pet_net_rssi(int *dbm);

/* 当前 AP 的 SSID（NUL 结尾，最多 32 字节）；空口字节不是可打印 UTF-8 时返回
   ESP_ERR_INVALID_ARG，由调用方省略该字段，避免污染遥测 JSON。 */
esp_err_t pet_net_ssid(char *out, size_t capacity);

/* 当前 AP 的主信道（1–196）；未关联或取不到时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pet_net_channel(int *channel);

/* 当前 AP 的 BSSID（大写十六进制，AA:BB:CC:DD:EE:FF；至少 18 字节）；未关联或
   取不到时返回 ESP_ERR_INVALID_STATE。仅用于识别接入点，不含口令。 */
esp_err_t pet_net_bssid(char *out, size_t capacity);

/* 当前 AP 的信道宽度（20 或 40 MHz），由主/辅信道推断；未关联或取不到时返回
   ESP_ERR_INVALID_STATE。 */
esp_err_t pet_net_channel_width(int *mhz);

/* 本次启动以来最近一次运行期断线原因（ESP-IDF wifi_err_reason_t）与设备时刻；
   epoch_seconds 在未对时时为 0。从未断线时返回 ESP_ERR_INVALID_STATE。 */
esp_err_t pet_net_last_disconnect(int *reason, uint32_t *epoch_seconds);

const char *pet_net_state_name(pet_net_state_t state);
const char *pet_net_error_name(pet_net_error_t error);
