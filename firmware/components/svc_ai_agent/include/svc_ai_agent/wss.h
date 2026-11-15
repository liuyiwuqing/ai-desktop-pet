#pragma once

#include "svc_ai_agent/api.h"

#define PET_XZ_WSS_URI_BYTES 255u
#define PET_XZ_WSS_TOKEN_BYTES 512u
#define PET_XZ_WSS_ID_BYTES 64u
#define PET_XZ_WSS_CA_BYTES 4096u

typedef struct {
    const char *uri; /* 必须为 wss://；不接受 URL userinfo 或 fragment。 */
    const char *token; /* 非空，允许原始 token 或 Bearer 前缀；不持久化。 */
    const char *device_id;
    const char *client_id;
    const char *ca_cert_pem; /* NULL 使用证书包；始终校验主机名。 */
} pet_xz_wss_config_t;

typedef struct {
    bool ready; /* codec/任务已就绪；不表示已连接或获云端授权。 */
    bool network_ready; /* Wi-Fi 已有 IP 且 SNTP 已对时，与 MQTT 状态无关。 */
    bool faulted; /* 资源初始化/销毁失败后锁住至重启。 */
    uint32_t turn_id, epoch;
    esp_err_t last_error, cleanup_error;
    uint32_t rejected_frames, stale_items, sent_packets, received_packets;
    /* 自启动累计 MCP 信封数；含发现/通知，不证明云端使用了上下文。 */
    uint32_t mcp_received, mcp_sent, mcp_failed;
    uint32_t owner_stack_free; /* ESP-IDF 任务栈最小剩余字节，含 vendor Opus 调用。 */
} pet_xz_wss_snapshot_t;

/*
 * 组合根一次性启动，字符串在返回前复制。ESP_OK 仅表示任务创建成功；
 * codec 异步结果看 snapshot。服务与队列持续至重启，无运行期重建 API。
 * 队列 payload 使用 PSRAM；缺少内存时明确失败，不回退到无界缓冲。
 * 返回的 transport 只供一个 A4 拥有者调用；非 ISR、非阻塞，不重入 A4。
 * begin 之后才联网，每轮全新物理连接，不自动重连或重放。没有唤醒或按键绑定。
 * 原始 PCM、文本和凭据不写盘；本模块不打印它们。
 */
esp_err_t pet_xz_wss_start(const pet_xz_wss_config_t *config, pet_ai_transport_t *transport);
esp_err_t pet_xz_wss_snapshot(pet_xz_wss_snapshot_t *snapshot);

/* 启动前组合根注册；回调只在网络任务执行，不能操作音频 HAL。
 * JSON-RPC 请求/响应不含传输外层；epoch 用于隔离连接去重。 */
typedef bool (*pet_xz_tool_handler_t)(uint32_t epoch,const char *request,size_t bytes,
    char *response,size_t capacity,size_t *written,void *context);
esp_err_t pet_xz_wss_set_tools(pet_xz_tool_handler_t handler,void *context);
