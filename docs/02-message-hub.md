# 02 · 万能信息接收端模块

状态：待评审；S5-A 设备侧纯逻辑、S5-B Wi-Fi/MQTT/TLS 传输层、S5-C1 inbox 收包接入、S5-C2 收件箱持久化已落地（2026-11-11）。依赖：`00-architecture.md`、`01-ai-agent.md`（表情联动）。

## 1. 范围

做：接收第三方消息、校验与去重、优先级队列、屏幕渲染、已接收/已显示/已读回执、在线配置管理面板、第三方推送接口。

不做：第三方平台的全部私信协议、任意网页抓取、无授权的账号登录、外部自动回复发送。

## 2. 技术方案对比

### 2.1 设备侧传输

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| MQTT over TLS + 服务端 HTTP Webhook | 持久会话、QoS 1、离线队列、低延迟、成熟运维；服务端可水平扩展 | 需要 Broker；主题 ACL 与证书要维护 | **推荐** |
| WebSocket 长连接 + 服务端 HTTP Webhook | 服务端实现简单；双向灵活 | 重连/心跳/离线队列/多设备扇出都要自研；弱网易丢消息 | 备选 |
| HTTP 轮询 | 实现最简单；无长连接 | 延迟高、功耗差、消息突发时体验差 | 不采用 |

### 2.2 服务端接入

| 方案 | 说明 | 结论 |
| --- | --- | --- |
| 复用 `nexus-core` | 已有 MQTT/TLS、设备认证、消息去重、管理台、Halo 接入 | **推荐首版** |
| 新建 `message-gateway` | 独立服务，按来源拆分 | 不采用；消息中心归 `nexus-core`，不再建第二套消息服务 |

### 2.3 推荐技术栈

- 设备：ESP-IDF `esp-mqtt`，MQTT 3.1.1 over TLS 8883，QoS 1，`clean_session=false`，keepalive 60 s，LWT。
- Broker：Mosquitto 2.0.22（与 `nexus-core` 现有 compose、ACL、集成测试一致）；单设备足够，集群或多租户再评估 EMQX。
- 服务端：Java 21 / Spring Boot / PostgreSQL / Flyway / Vue 3（复用 `nexus-core`）。
- 第三方入口：HTTPS Webhook + Bearer Token + HMAC 签名；管理台手动测试发送。
- 实时状态：管理台用 SSE；失败回退 5 s 轮询。

## 3. 主题与协议

### 3.1 MQTT 主题

| 主题 | 方向 | QoS | 说明 |
| --- | --- | --- | --- |
| `nexus/v1/devices/{deviceId}/inbox` | 服务端 → 设备 | 1 | 消息投递 |
| `nexus/v1/devices/{deviceId}/ack` | 设备 → 服务端 | 1 | received/displayed/read |
| `nexus/v1/devices/{deviceId}/status` | 设备 → 服务端 | 1 | 在线、固件、队列、传感器摘要；LWT |
| `nexus/v1/devices/{deviceId}/config` | 服务端 → 设备 | 1 | 保留消息，DND/音量/模板开关 |

主题 ACL：设备只能读写自己的 `{deviceId}` 子树；禁止共用 CORE 管理员账号。

### 3.2 消息信封

Schema：`config/schemas/message.schema.json`；示例：`config/examples/message.example.json`。

```json
{
  "schemaVersion": 1,
  "id": "msg_01JABCDEF",
  "source": "halo.comment",
  "type": "comment",
  "priority": "normal",
  "title": "新评论",
  "body": "这篇文章很有帮助，想请教一下……",
  "payload": { "postTitle": "ESP32 入门", "author": "读者" },
  "createdAt": "2026-11-10T10:00:00+08:00",
  "expiresAt": "2026-11-11T10:00:00+08:00",
  "dedupeKey": "halo:comment:123456",
  "display": {
    "template": "comment_card",
    "durationMs": 8000,
    "sound": "chime",
    "icon": "comment"
  },
  "requireAck": true
}
```

字段约束：

- `id` 全局唯一，服务端生成；`dedupeKey` 可选，用于跨重投去重。
- `priority`：`critical|high|normal|low`；只有 `critical` 可越过 DND。
- `body` ≤2 KB；整包 ≤8 KB（服务端 413 上限）；设备侧解析上限 4 KB（`PET_MESSAGE_JSON_MAX`），超限丢弃并记录日志。
- `expiresAt` 过期不展示；设备本地也校验（时间未同步时跳过 TTL 判定）。
- `display.durationMs` 5–30 s；`template` 必须在设备支持列表内，否则回退 `text_card`。
- 未知字段与重复字段拒收（schema `additionalProperties:false`）；`schemaVersion` 不支持时拒收并上报。

### 3.3 回执

Schema：`config/schemas/message-ack.schema.json`；示例：`config/examples/message-ack.example.json`。

```json
{ "schemaVersion": 1, "messageId": "msg_01JABCDEF", "state": "received", "at": "2026-11-10T10:00:01+08:00", "queueDepth": 0 }
```

| 状态 | 触发 | 语义 |
| --- | --- | --- |
| `received` | 消息写入 NVS 成功 | 设备已持久接收，可安全重投 |
| `displayed` | 完整帧渲染完成并上屏 | 用户可见，不代表已读 |
| `read` | 消息可见时 BOOT 短按/触摸确认 | 用户已读 |

`displayed` 失败不得发送；渲染超时/失败只保留 `received`，服务端可重试。

### 3.4 服务端保留与隐私

- 全文（标题、正文、原始 payload）保留 7 天；消息 ID、来源、设备、优先级、状态、时间等元数据保留 30 天。
- 到期由每日清理任务删除；审计日志只记录消息 ID 与操作，不含正文。
- 管理台默认只导出元数据；导出正文需显式勾选并写审计。
- 设备未 ACK 的消息在保留窗口内可重投；超出窗口按失败处理并记录。

## 4. 设备侧队列与去重

- 未读队列上限 8 条（默认值，P3 实机压测后调整）；按 `priority` 降序、`createdAt` 升序。
- 队列满：丢弃最低优先级最旧消息；`critical` 不丢，除非同 ID 重复。
- 持久化：NVS `msg_inbox`，双代 A/B 快照 + CRC32；快照落盘成功才 ACK `received`，失败保留 dirty 并由服务端重投补发。
- 去重环：最近 16 个 `id`/`dedupeKey`，随快照掉电保持；重复消息只更新状态，不重复展示。
- TTL：展示前检查 `expiresAt`；过期直接丢弃并 ACK `read`（避免服务端重投）。
- 损坏：单代损坏回退另一代；两代都损坏时以空队列启动并告警，不自动擦除；后续快照会覆盖非活动代。
- DND：`normal/low` 只入队不打扰；`high` 可亮屏不发声；`critical` 正常提醒。

## 5. 在线配置管理面板

### 5.1 功能清单

| 页面 | 功能 |
| --- | --- |
| 概览 | 设备在线数、离线数、今日消息量、失败率、队列积压、最近告警 |
| 设备 | 列表、注册、启停、重命名、查看固件/IP/RSSI/队列、轮换凭据、OTA 入口 |
| 消息源 | Halo 评论、Webhook、RSS、天气、自定义；启停、凭据、测试连接、最近事件 |
| 路由规则 | 来源 → 设备、关键词/正则过滤、优先级、模板、DND、时间窗 |
| 消息模板 | 标题/正文/图标/声音/时长；变量预览；设备能力校验 |
| 消息中心 | 收件箱、状态筛选、重发/取消、投递时间线、导出 |
| 自动化 | 触发条件、动作、启停、测试运行、执行历史 |
| 设置 | DND 时段、音量、亮度、时区、AI 供应商、隐私开关 |
| 审计 | 登录、配置变更、消息重发、设备操作；不含凭据 |

### 5.2 交互设计

- 左侧导航 + 顶部设备选择器；列表页表格 + 右侧详情抽屉。
- 首次引导：注册设备 → 配置 Broker/凭据 → 发送测试消息 → 查看回执。
- 测试发送：选择设备、模板、优先级；提交后显示 `queued → sent → received → displayed → read` 时间线。
- 危险操作（删除设备、轮换凭据、清空队列）二次确认，输入设备名。
- 空状态给出下一步动作；加载用骨架屏；错误显示可重试原因。
- 窄屏 390 px：表格转卡片，操作收进“更多”；不横向溢出。
- 权限：`admin` 全部；`operator` 消息/规则；`viewer` 只读。
- 实时：设备状态 SSE 推送；断线自动重连，回退轮询。

## 6. 第三方推送接口规范

### 6.1 发送消息

```http
POST /api/v1/messages
Authorization: Bearer <source-token>
Content-Type: application/json
Idempotency-Key: <可选，建议 UUID>
X-Nexus-Signature: sha256=<hmac>
```

- `source-token` 按来源签发，最小权限；只允许写入绑定的设备/路由。
- 签名：`HMAC-SHA256(timestamp + "." + rawBody, sourceSecret)`；时间戳容差 5 分钟；防重放。
- 成功：`202 Accepted`，返回 `{ "id": "...", "status": "queued" }`。
- 错误：`400` 参数错误、`401` 未认证、`403` 无权限、`409` 幂等冲突、`413` 超限、`429` 限流。
- 限流：每来源 60 条/分钟，突发 20；超限返回 `Retry-After`。
- 重试：5xx/超时按指数退避重试 3 次；4xx 不重试。
- 查询状态：`GET /api/v1/messages/{id}` 返回投递状态与时间线。
- 测试：`POST /api/v1/sources/{id}/test` 只发测试消息，不进入生产统计。

### 6.2 来源接入

| 来源 | 接入方式 | 备注 |
| --- | --- | --- |
| Halo 评论 | 官方只读 Console API 定时扫描 | 仅公开、已审核、未隐藏 |
| 通用 Webhook | HTTP + HMAC | 第三方按信封提交 |
| RSS/Atom | 服务端定时拉取 | 去重按 guid/link |
| 天气 | Open-Meteo 等 HTTPS | 只做提醒，不伪造室内数据 |
| 自定义 | 管理台手动/API | 需来源令牌 |

## 7. 消息渲染逻辑

### 7.1 模板

| 模板 | 用途 | 布局 |
| --- | --- | --- |
| `text_card` | 通用文本 | 图标 + 标题 2 行 + 正文 6 行 + 来源/时间 |
| `comment_card` | 评论 | 头像占位 + 作者 + 正文 + 文章标题 |
| `alert_card` | 传感器/系统告警 | 红色边框 + 告警级别 + 建议动作 |
| `growth_card` | 成长事件 | 图标 + 数值变化 + 宠物表情 |
| `system_card` | 固件/网络 | 状态码 + 简短说明 + 操作提示 |

### 7.2 展示规则

- 单条全屏卡片；正文超出 6 行进入纵向滚动，底部显示页码/进度。
- 标题最多 2 行，超出省略；正文按 UTF-8 字符换行，避免截断多字节字符。
- 展示时长：`max(5000, min(30000, 字数 × 80 ms))`，可由 `display.durationMs` 覆盖。
- 优先级抢占：`critical` 立即打断当前消息并红色边框抖动；`high` 当前消息结束后优先；`normal` FIFO；`low` 仅在空闲时展示。
- 消息到达：短提示音 + 惊讶表情；`critical` 使用警报音 + 抖动；DND 下静默。
- 已读：消息可见时 BOOT 短按；自动滚动到底部不自动已读。
- 渲染失败：100 ms 超时回退纯文本；仍失败则保留队列并上报，不 ACK `displayed`。
- 字体：中文 16/24 px 子集；数字等宽；缺字回退点阵并记录缺字表。

### 7.3 状态机

```text
IDLE → RECEIVED → QUEUED → RENDERING → DISPLAYED → READ
                      │          │
                      │          └→ RENDER_FAILED（保留队列，可重试）
                      └→ EXPIRED/DROPPED（低优先级/队列满）
```

## 8. 实现步骤

| 步骤 | 内容 | 产出 |
| --- | --- | --- |
| 1 | 定义信封/回执 JSON Schema 与数据库迁移 | schema、Flyway 脚本 |
| 2 | 设备 MQTT 连接、TLS、主题 ACL | 在线状态、LWT |
| 3 | NVS 收件箱、去重环、ACK | 掉电不丢、重复不展示 |
| 4 | 渲染器 + 模板 + 队列 + DND | 五种模板、优先级抢占 |
| 5 | 管理台设备/消息/模板/测试发送 | 端到端时间线 |
| 6 | Webhook 入口 + HMAC + 限流 | 第三方推送可用 |
| 7 | 异常：离线、突发、超大、过期、损坏 | 降级与恢复 |
| 8 | HIL：断网、重连、24 h | 验收记录 |

## 9. 验收标准

### 功能

- Webhook 到设备上屏 ≤2 s（局域网、正常 Broker）。
- 同一 `id`/`dedupeKey` 重投 10 次只展示 1 次。
- 离线期间 10 条消息，重连后按优先级展示最多 8 条；低优先级被丢弃且有计数。
- `critical` 越过 DND；`normal` 遵守 DND；`expiresAt` 过期不展示。
- 管理台测试发送能看到 `queued → sent → received → displayed → read` 全链路。
- 未授权、签名错误、超限、超大请求分别返回 401/403/429/413。
- 渲染失败不 ACK `displayed`，恢复后可重试。
- 390 px 管理台无横向溢出；键盘可完成主要操作。

### 稳定性

- 消息突发 100 条/分钟持续 10 分钟：设备不重启、队列不越界、无内存泄漏。
- 断网 5 分钟后恢复：自动重连，未读消息不丢（在队列上限内）。
- 24 h soak：NVS 写入次数在预算内，无看门狗复位。
- 日志不含消息全文（可配）、令牌、签名密钥。

### 测试

- 主机测试：信封校验、优先级、TTL、去重、队列满、渲染回退。
- 契约测试：Webhook 签名、幂等、限流、状态时间线。
- HIL：真实 Broker + 设备 + 管理台，断网/重连/突发。

## 10. 风险与待确认

| 风险 | 影响 | 缓解 |
| --- | --- | --- |
| Broker 运维与证书轮换 | 设备离线 | 自动化证书检查、双证书过渡、管理台告警 |
| NVS 写入磨损 | 队列损坏 | 有界队列、整代快照写、CRC、双代 A/B |
| 消息风暴 | UI 卡顿/丢消息 | 限流、优先级、合并低价值消息 |
| 第三方平台权限变化 | 来源失效 | 来源健康检查、错误可见、降级停用 |
| 中文换行/字体 | 截断/乱码 | 子集字体、UTF-8 换行、缺字回退 |
| 管理台权限过宽 | 误操作 | RBAC、二次确认、审计 |

已确认（Q3=C / B 组）：消息中心与管理台复用 `nexus-core`，不新建 `message-gateway`；Broker 用 Mosquitto 2.0.22；消息全文保留 7 天、元数据保留 30 天。设备未读队列上限 8 条为默认值，P3 压测后调整。

## 11. 落地记录

### S5-A 设备侧纯逻辑（2026-11-10）

已落地：

- `contracts/include/pet/message.h`：消息视图、优先级、模板/声音、ACK 状态与事件 payload。事件只带 `id + state + queueDepth`（56 B），正文通过消息中心快照读取。
- `svc_message_hub`：ISO-8601 子集解析、信封字段校验、8 条有界优先级队列（优先级降序、创建时间升序）、16 槽 FNV-1a 去重环、TTL 清理、DND 通知策略、ACK 状态机。
- 主机测试：`test_message_time`、`test_message_validate`、`test_message_queue`；默认构建 0 warning，镜像 242,052 B，DIRAM 69,650 B。

内存约束：`pet_message_view_t` 为 2,448 B，8 条队列存储约 19.1 KB；S5-C 持久化与运行期队列应分配到 PSRAM，避免占用内部 heap 预算。

未落地：Wi-Fi/MQTT/TLS 传输、NVS `msg_inbox` 持久化、ACK 发布、消息渲染与 UI 接入、实机断网/重连验收。

### S5-B Wi-Fi + MQTT/TLS 传输层（2026-11-10）

已落地：

- `contracts/include/pet/net.h`：网络状态/错误枚举、`pet_net_config_t`（SSID、Broker URI、设备 ID、用户名/令牌、可选 PEM CA、keepalive、重连间隔）与 8 B 状态事件；事件总线新增 `PET_EVT_NET_STATE` / `PET_EVENT_SOURCE_NET`。
- `pet_hal/include/pet/hal/net.h`：`pet_net_t` 驱动接口（`start`/`stop`/`publish`/`is_online`），提供 ESP 与 fake 两个实现。
- `svc_net`：纯逻辑策略（设备 ID、配置边界、`nexus/v1/devices/{id}/inbox|ack|status|config` 主题构建）+ 服务层与驱动选择。
- 真实驱动 `net_esp.c`：Wi-Fi station（`WIFI_STORAGE_RAM`，断线指数退避 2–30 s）、esp-mqtt 3.1.1 over TLS 8883、QoS 1、`clean_session=false`、keepalive 60 s、LWT（retained `{"online":false}`）、自动重连；GOT_IP 后连接 Broker，上线后发布 retained status 并订阅 inbox/config。
- fake 驱动（`CONFIG_PET_NET_FAKE`）：无硬件时复用同一状态机发布 CONNECTING/ONLINE，便于桌面验证。
- 凭据：从 NVS `net`/`nexus` 读取，Kconfig 仅作开发期回退；未配置 SSID/URI 时只告警，不阻断启动。
- 主机测试 `test_net_policy`：设备 ID、配置边界、四种主题与缓冲区不足。
- 构建：默认镜像 1,023,456 B，DIRAM 126,154 B（36.91%）；fake 镜像 245,072 B；关闭网络模块镜像 242,056 B，ELF 中无 `pet_net_*` 符号。

未落地：inbox 载荷 JSON 解析与消息中心接入、ACK 发布、`msg_inbox` 持久化、证书轮换与服务端 ACL 配套、实机断网/重连与 24 h 验收。TLS 与 Wi-Fi/MQTT 任务使镜像与内部 RAM 明显上升，实机需重点观察 heap 最低水位。

### S5-C1 inbox 收包接入（2026-11-11）

已落地：

- `pet/net.h` 公开主题枚举；`svc_net` 新增 `pet_net_set_rx_handler`、`pet_net_publish_topic`、`pet_net_is_time_synced`，收包回调携带 topic/payload/length/retain。
- `net_esp.c`：GOT_IP 后先做 SNTP（`pool.ntp.org`，15 s 超时），同步成功或超时后再连 Broker，避免持久会话在未对时时投递 inbox；MQTT DATA 按 `current_data_offset`/`total_data_len` 重组，4 KB 上限，超长丢弃；retain 标志透传。
- `svc_message_hub`：`message_codec.c` 严格 JSON 解析（4 KB 上限；必填字段缺失、未知/重复字段、非法转义、孤立 UTF-16 代理与 `\u0000` 均拒收；`payload` 只跳过不保存）；`pet_message_ack_format` 输出 received/displayed/read；`message_time.c` 新增 UTC 格式化。
- `message_service.c`：8 槽队列优先分配 PSRAM（失败回退内部 RAM），互斥锁保护；提供 `pet_message_hub_ingest/ack/snapshot/get/depth`；入队成功后发布 `PET_EVT_MSG_RECEIVED` 与 `received` ACK；过期消息 ACK `read` 且不入队；重复与队列满不 ACK。
- `app.c`：初始化消息中心，注入 ACK 发布器与 inbox 收包回调；时间未同步时以 `createdAt` 作为 ACK 时间。
- 测试：新增 `test_message_codec`（完整/最小信封、转义与代理对、未知/重复字段、枚举/时长/时间/长度边界、ACK 格式化）；`test_message_time` 补充格式化与回环用例。
- 构建：默认镜像 1,037,308 B，DIRAM 130,618 B（38.22%）；fake 镜像 254,544 B；关闭网络 242,176 B；主机测试全部通过。

未落地：`msg_inbox` 持久化与启动恢复（C2）、`displayed/read` 的 UI 触发、retained config 下发、服务端 ACL/证书轮换、实机收包与 heap 水位验收。

### S5-C2 收件箱持久化（2026-11-11）

已落地：

- 分区：NVS 扩至 64 KB（0x9000/0x10000），`otadata` 顺延到 0x19000/0x2000、`phy_init` 到 0x1B000/0x1000；`ota_0` 仍为 0x20000，OTA 槽位不变。
- `message_store_codec.c`：固定小端快照格式，20 B header + 16×8 B 去重环 + count×2,429 B 记录；header 含 magic、layoutVersion、recordSize、count、dedupeNext、payloadLength 与 CRC32(IEEE)，CRC 覆盖 header 前 16 B 与载荷；解码校验 magic/版本/记录长度/计数/总长/CRC，并拒绝非法枚举与非 NUL 结尾字符串。
- `message_store.c`：NVS `msg_inbox`，`snap0`/`snap1` 双代 + `active` 指针；先写非活动代，成功后再切换 active；启动读取失败回退另一代，两代都损坏时以空队列启动并告警；编码缓冲区优先 PSRAM。
- `message_queue_restore`：恢复时逐条重新校验、跳过过期/损坏记录，载入去重环但不二次写入；恢复后仍能识别服务端重投。
- `message_service.c`：启动时恢复快照；入队、状态变化与过期清理后持久化；`received` ACK 仅在快照落盘成功后发送，失败保留 dirty 等待重投补发；重复消息仍在队列时补发 `received`；`displayed/read` 即使落盘失败也发送，dirty 由后续变更重试。
- `app.c`：MQTT 上线（此时 SNTP 已完成）后清理持久化消息中的过期项。
- 测试：新增 `test_message_store`（空/多条快照回环、CRC/魔数/版本/记录长度/截断/容量错误、非法枚举）；`test_message_queue` 增加恢复与去重环用例。
- 构建：默认镜像 1,041,828 B、DIRAM 130,618 B（38.22%）；fake 258,912 B；关闭网络 242,176 B；主机测试全部通过。

未落地：实机掉电恢复与 NVS 磨损验收、24 h soak、`displayed/read` 的 UI 触发、retained config 下发、服务端 ACL/证书轮换。
