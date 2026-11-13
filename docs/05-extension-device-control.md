# 05 · 扩展一：语音控制手机/电脑

状态：首版 Mac 适配已实现（2026-11-12）：设备发现、`system.info`、`project.status`、需本地确认的 `app.open`，并具备请求去重、审计与未知结果查询。扩展默认关闭；[Gateway](../server/README.md) 与 Bridge 尚未在真实设备链路验收。手机、Home Assistant 及下文更多能力属于后续设计。依赖：`00-architecture.md`、`01-ai-agent.md`。

## 1. 范围

做：定义设备能力模型、意图格式、适配器接口、风险分级、确认机制、审计与错误码；为后续接入 Mac、iPhone、Home Assistant 等平台预留边界。

不做：任意 shell/脚本执行、绕过平台授权的私有接口、无确认的破坏性操作、读取用户隐私数据。

## 2. 能力模型

### 2.1 设备与能力

```c
typedef struct {
    char id[32];                 // "mac", "iphone", "home"
    char name[64];
    char type[24];               // computer / phone / home
    risk_level_t max_risk;       // 该设备允许的最高风险
    const char *adapter_id;      // "mac_bridge", "ios_shortcuts", "home_assistant"
} device_descriptor_t;

typedef struct {
    char name[64];               // "app.open"
    const char *params_schema;   // JSON Schema 字符串或路径
    risk_level_t risk;
    bool requires_confirmation;
    uint16_t timeout_ms;
} capability_descriptor_t;
```

能力示例：

| 能力 | 参数 | 风险 | 确认 |
| --- | --- | --- | --- |
| `system.info` | 无 | L0 只读 | 否 |
| `project.status` | `projectId` | L0 | 否 |
| `app.open` | `app`（白名单 ID） | L1 可逆 | 是 |
| `media.play_pause` | `target` | L1 | 否 |
| `phone.call` | `contactId` | L2 破坏性/外部影响 | 是 |
| `shortcut.run` | `shortcutId` | L1/L2 按配置 | 按配置 |

### 2.2 风险分级

| 级别 | 定义 | 例子 | 策略 |
| --- | --- | --- | --- |
| L0 | 只读、无副作用 | 系统信息、项目状态 | 直接执行 |
| L1 | 可逆、本机影响 | 打开应用、播放/暂停 | 默认确认；可配置免确认 |
| L2 | 破坏性/外部影响 | 拨打电话、发送消息、删除文件 | 强制二次确认 + 审计 |
| L3 | 禁止 | 任意 shell、格式化、提权 | 不注册、不执行 |

## 3. 请求/结果契约

```c
typedef struct {
    char request_id[40];         // 幂等键
    char device_id[32];
    char capability[64];
    cJSON *params;               // 已通过 schema 校验
    risk_level_t risk;
    uint32_t timeout_ms;
    char confirmation_token[65]; // L2 必填
    char source_turn_id[40];     // 关联对话轮次
} command_request_t;

typedef enum {
    CMD_OK, CMD_DENIED, CMD_TIMEOUT, CMD_UNKNOWN, CMD_FAILED
} command_status_t;

typedef struct {
    command_status_t status;
    cJSON *output;               // 结构化结果，不含凭据
    char error_code[32];
    char audit_id[40];
} command_result_t;
```

JSON 意图（由 LLM 产出，不直接执行）：

```json
{
  "intent": "device.control",
  "confidence": 0.93,
  "params": {
    "deviceId": "mac",
    "capability": "app.open",
    "args": { "app": "idea" }
  },
  "requiresConfirmation": true,
  "sourceTurnId": "turn-18"
}
```

## 4. 适配器接口

```c
typedef struct {
    const char *adapter_id;
    esp_err_t (*discover)(device_descriptor_t *out, size_t max, size_t *count);
    esp_err_t (*execute)(const command_request_t *req, command_result_t *out);
    esp_err_t (*health)(bool *online);
} command_adapter_t;
```

计划适配器：

| 适配器 | 平台 | 传输 | 说明 |
| --- | --- | --- | --- |
| `mac_bridge` | macOS | HTTP 127.0.0.1:8091 | 复用现有白名单、确认、SQLite 审计 |
| `ios_shortcuts` | iPhone | HTTPS Shortcut/Webhook | 需用户授权与 Shortcut 配置 |
| `home_assistant` | 智能家居 | REST/WebSocket | 长令牌、实体白名单 |
| `mqtt_device` | 自定义设备 | MQTT | 主题 ACL、QoS 1 |

适配器只接受已注册能力与已校验参数；禁止把原始自然语言、文件路径、URL 直接传给适配器。

## 5. 语音控制流程

```text
用户说话
  → ASR 文本
  → LLM 产出 intent JSON
  → intent_router 校验：意图/能力/参数/风险
  → L0：直接调用适配器
  → L1/L2：屏幕确认 + 语音确认
  → 适配器执行（超时/幂等/审计）
  → 结果转成短句 + 表情
```

确认交互：

- 屏幕显示“要打开 IDEA 吗？”+ 是/否；语音说“确认/取消”。
- 确认令牌绑定 `request_id + device + capability + params 摘要 + 过期时间`；默认 60 s。
- 超时/拒绝返回 `CMD_DENIED`，不执行。
- 结果未知（网络中断）：提示“我不确定是否成功”，提供“核对结果”查询，不自动重试破坏性操作。

## 6. 安全与审计

- LLM 不能直接执行命令；只产出意图，路由层做 schema 与白名单校验。
- 提示注入防护：用户文本/第三方消息中的“执行命令”不作为系统指令；只接受结构化 intent。
- 凭据：适配器令牌存 NVS/私有 env；日志脱敏；不把令牌传给 LLM。
- 审计：每次执行记录 `request_id`、设备、能力、参数摘要、风险、确认人、结果、耗时；保留 30 天。
- 限流：每设备 30 次/分钟；L2 每 10 分钟最多 3 次。
- 失败关闭：适配器离线/鉴权失败时禁用对应能力，不降级为任意执行。

## 7. 前置架构交付物

- `extensions/device_control/include/command_adapter.h`：上述接口声明。
- `extensions/device_control/include/capability_registry.h`：注册/查找/校验。
- `extensions/device_control/README.md`：能力表、风险分级、适配器接入步骤。
- `config/schemas/command_request.schema.json`、`capability.schema.json`。
- Mock 适配器 + 主机测试：未知能力、L2 缺确认、超时、幂等、审计。

本阶段只写接口与 mock，不接真实平台、不打开应用、不拨打电话。

## 8. 实现步骤（后续）

| 步骤 | 内容 | 产出 |
| --- | --- | --- |
| 1 | 契约 + 能力注册 + mock 适配器 | 主机契约测试 |
| 2 | Mac Bridge L0 只读接入 | `system.info`/`project.status` |
| 3 | `app.open` L1 + 确认 | 真实打开白名单应用 |
| 4 | 语音意图接入 | “打开 IDEA”端到端 |
| 5 | iOS Shortcuts / HA 适配 | 独立授权与验收 |
| 6 | 审计/限流/安全评审 | 渗透与误操作测试 |

## 9. 验收标准（接口阶段）

- Mock 适配器契约测试通过：注册、发现、执行、超时、幂等、错误码。
- 未知能力/未注册设备/参数不符合 schema 一律拒绝，不调用适配器。
- L2 缺少确认令牌或令牌过期 → `CMD_DENIED`，审计有记录。
- 同一 `request_id` 重复执行只产生一次副作用，返回原结果。
- 适配器超时 5 s 返回 `CMD_TIMEOUT`，UI 有明确反馈。
- 日志与审计不含令牌、完整参数中的敏感字段。
- 代码中不存在任意 shell、任意路径、任意 URL 执行入口。

## 10. 风险与待确认

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| 提示注入 | 越权执行 | 意图白名单、schema、风险分级、确认 |
| 平台授权变化 | 适配器失效 | 健康检查、能力禁用、错误可见 |
| 确认绕过 | 误操作 | 令牌绑定参数 + 过期 + 审计 |
| 结果不确定 | 重复副作用 | 幂等键、查询核对、不自动重试 L2 |
| 令牌泄露 | 越权 | NVS/私有 env、脱敏、轮换 |

待确认：首批接入平台（Mac/iPhone/HA）、允许的能力清单、L1 是否免确认、审计保留天数、是否需要服务端统一鉴权。

## 2026-11-12 当前实现

当前仅注册 system.info、project.status、app.open，分别为 L0/L0/L1；app.open 需屏幕展示后新的 BOOT 长按确认。已接严格白名单、持久水位、去重/冲突、限速、审计、UNKNOWN 查询和 Mac Bridge HTTPS 薄网关。无任意 shell、破坏性能力或手机平台执行器。扩展默认关闭，部署与凭据见 [扩展说明](../extensions/device_control/README.md)。纯引擎/确认/LVGL/Gateway 通过主机检查，真实 Bridge 与 TLS 未联调。
