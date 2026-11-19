# 02 · 万能信息接收端模块

状态：待评审；S5-A 设备侧纯逻辑、S5-B Wi-Fi/MQTT/TLS 传输层、S5-C1 inbox 收包接入、S5-C2 收件箱持久化、S5-D 首版消息界面与 BOOT 已读、S5-E 持久回执、S5-F nexus-core v1 协议适配已落地（2026-11-11）；源码契约回环通过，真实 Broker 与实机验收待进行。依赖：`00-architecture.md`、`01-ai-agent.md`（表情联动）。

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

| 主题 | 方向 | QoS / retain | 说明 |
| --- | --- | --- | --- |
| `nexus/v1/devices/{deviceId}/inbox` | 服务端 → 设备 | 1 / false | `inbox.message` 信封 |
| `nexus/v1/devices/{deviceId}/acks` | 设备 → 服务端 | 1 / false | received/displayed/read 或 rejected |
| `nexus/v1/devices/{deviceId}/state` | 设备 → 服务端 | 1 / true | schemaVersion/deviceId/online/observedAt；LWT |
| `nexus/v1/devices/{deviceId}/config` | 服务端 → 设备 | 规划 | 现有服务端尚未提供配置下发契约，设备未应用 |

`deviceId` 使用服务端的 `[a-zA-Z0-9_-]{1,40}`。收件时校验完整主题、信封目标设备与非 retained 属性；设备只能读写自己的主题，Broker ACL 仍是部署侧的独立防线。

### 3.2 消息信封

当前线协议以 [`integration/contracts/nexus-v1/`](../integration/contracts/nexus-v1/) 为准，`config/schemas` 与示例是 CI 校验一致的镜像。旧 `msg_…` 信封保存在 `legacy-device-v1`，不再接收为线上消息。

```json
{
  "schemaVersion": 1,
  "type": "inbox.message",
  "eventId": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee",
  "deviceId": "dodo-01",
  "sentAt": 1794362400,
  "expiresAt": 1794448800,
  "payload": {
    "messageId": "11111111-2222-4333-8444-555555555555",
    "source": "halo",
    "eventType": "comment.created",
    "title": "博客有新评论",
    "summary": "访客：很喜欢这个桌宠项目。",
    "priority": "normal"
  }
}
```

- `payload.messageId` 与 `eventId` 保持服务端小写标准 UUID；不截断、散列或替换成 `msg_…`。
- `sentAt/expiresAt` 为 UTC epoch 秒，设备范围为正 uint32，且 `expiresAt > sentAt`。时间未同步时暂不接受新投递，等待服务端重试。
- JSON 总长不超过 1024 UTF-8 字节；`title` 不超过 96 字节，`summary` 不超过 180 字节。服务端 `DeviceText.fit` 在 UTF-8 边界裁剪，并将控制字符替换为空格。
- `priority` 为 `low/normal/high/critical`；UI 按 `eventType` 的 `comment./alert./growth./system.` 前缀映射模板，其他类型使用文本卡片。`payload.display` 可选，承载模板的非缺省 `sound`（`none/chime/alert`）与 `durationMs`（5000–30000）；缺省等价于 `none`/8000，DND 策略保持不变。
- 未知字段、重复字段、非法 UTF-8、无效身份、版本和时间范围均拒收；失败不会产生成功回执。

### 3.3 回执与在线状态

```json
{"schemaVersion": 1, "deviceId": "dodo-01", "eventId": "aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee", "messageId": "11111111-2222-4333-8444-555555555555", "status": "read"}
```

| status | 触发 | 语义 |
| --- | --- | --- |
| `received` | 收件箱与待发回执原子落盘成功 | 设备已接管消息；服务端停止重投 |
| `displayed` | 完整帧渲染完成并上屏 | 用户可见，不代表已读 |
| `read` | 可见消息经 BOOT 短按确认 | 用户已读；离线时先落盘 |
| `rejected` + `reason=expired` | TTL 到期 | 过期，不冒充用户已读 |

同一投递的消息、事件、设备身份贯穿持久化与回执。后台只在 PUBACK 后更新待发记录；`received/displayed` 确认后移除回执，已确认的终态作为有界记录保留，重投时可再次回复 read/expired。PUBACK 只确认 Broker 接收，源码契约测试也不能证明真实数据库已经入库。

状态报文含 `schemaVersion=1`、`deviceId`、`online`、`observedAt`。对时后上线及每 30 秒发布 retained 在线快照；未对时不发送伪造的在线时间。LWT 使用 offline/observedAt=0，正常停机先有界等待 offline 确认。固件、队列和传感器摘要属于后续协议扩展。

### 3.4 服务端保留与隐私

- 全文（标题、正文、原始 payload）保留 7 天；消息 ID、来源、设备、优先级、状态、时间等元数据保留 30 天。
- CORE 已实现按接收时间计算的保留任务，默认关闭；备份和只读预览后显式启用，默认 UTC 04:00 执行，每批最多清除 200 条正文及删除 200 条元数据。清理审计只记录 actor、时间与计数，不含正文，见 [13](13-operational-software.md)。
- 管理台已提供按状态/来源或设备/CORE 收件窗口筛选的元数据 CSV，最多 5000 行，不含标题、正文或原始 payload，见 [CONSOLE-13](23-console-completion.md#console-13--更多筛选与元数据-csv)。正文导出授权仍属规划。
- 设备未 ACK 的消息在投递有效期内可重投；正文清理将未完成投递标记为 expired。外部模型/OpenViking 副本不由本地清理删除。

## 4. 设备侧队列与去重

- 未读队列上限 8 条；按 `priority` 降序、首次 `sentAt` 升序。
- 新协议收件箱满时拒收新投递，由服务端重试；不挤掉已经 ACK received 的消息。优先级仍决定已接受消息的展示顺序。
- 持久化：NVS `msg_inbox`，双代 A/B v3 快照 + CRC32；收件与回执一起落盘才接受，失败保留此前状态。
- 去重环保留最近 16 个 ID 指纹；队列与回执记录另按完整 ID/投递身份校验。回执区最多 16 条，已确认终态可淘汰，未确认回执不能被覆盖；去重与终态重放均为有界保留。
- TTL：展示前检查 `expiresAt`；清理消息与 rejected/expired 回执原子提交，回执区满则保留快照重试。旧本地消息不产生新协议回执。
- 损坏：单代损坏回退另一代；两代都损坏时以空队列启动并告警，不自动擦除；后续快照会覆盖非活动代。
- DND：`normal/low` 只入队不打扰；`high` 可亮屏不发声；`critical` 正常提醒。

## 5. 在线配置管理面板

本节是完整产品规划，不是已完成功能清单。当前管理台为十二个导航；实际验收覆盖、首版差距与逐项待办见 [23 · 管理台完成度](23-console-completion.md#console-02--当前实现与完整规划的差距)。只读概览、投递状态时间线、受限取消、组合筛选/元数据 CSV、路由、服务端模板管理、角色/审计、自动化和设备维护均有软件实现，原始要求继续保留。

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
- 测试发送：消息页选择优先级（普通为默认）与目标设备（默认按当前路由；显式选择只投递到这些启用设备）；模板测试仍在路由页；提交后显示逐设备投递状态，完整 `queued → sent → received → displayed → read` 时间线在投递页。
- 危险操作（删除设备、轮换凭据、清空队列）二次确认，输入设备名。
- 空状态给出下一步动作；加载用骨架屏；错误显示可重试原因。
- 窄屏 390 px：表格转卡片，操作收进“更多”；不横向溢出。
- 权限：`admin` 全部；`operator` 消息/规则；`viewer` 只读。
- 实时：设备状态 SSE 推送；断线自动重连，回退轮询。

## 6. 第三方推送接口规范

本节是已落地的第三方来源契约（CONSOLE-06-A）。通用来源入口为 `POST /api/v1/hooks/{sourceId}`，使用来源令牌 + HMAC + 时间戳 + 限流，成功返回 202；管理员 `POST /api/v1/messages` 仍是独立的 Bearer 管理入口（同 `connectorId + eventId` 重放 200），不采用来源鉴权。实际字段、错误码与边界以 [CORE 来源契约](../../nexus-core/docs/webhook-sources.md) 为准；CONSOLE-04 的管理员投递历史接口不等于来源访问契约。

### 6.1 发送消息

```http
POST /api/v1/hooks/{sourceId}
Authorization: Bearer <source-token>
Content-Type: application/json
X-Nexus-Timestamp: <Unix 秒，10 位>
X-Nexus-Signature: sha256=<64 位小写十六进制 HMAC>

{"eventId":"<UUID>","eventType":"alert.test","title":"...","content":"...","priority":"normal"}
```

- `source-token` 按来源签发，最小权限；只允许写入绑定的设备范围，并与当前路由结果取交集。
- 签名：`HMAC-SHA256(timestamp + "." + rawBody, sourceSecret)`；时间戳容差 ±300 秒；未通过令牌/签名/时间窗 401，来源停用 403。
- 成功：`202 Accepted`，返回 `{ "id": "...", "status": "queued", "duplicate": false }`。
- 错误：`400` 内容错误、`401` 未认证、`403` 来源停用、`409` 同 UUID 不同字节、`413` 超过 32768 字节、`429` 限流（带 `Retry-After: 60`）。
- 限流：每秒 1 令牌、桶容量 20，滚动 60 秒最多 60 条；已接受的相同事件重放不重复扣额。
- 重试：5xx/超时用同一 `eventId` 和完全相同 JSON 字节重试，返回相同消息 ID；重新序列化导致字节不同会 409。
- 查询状态：`GET /api/v1/hooks/{sourceId}/messages/{messageId}` 使用同来源 Bearer，返回每设备当前状态与事件时间线（按设备、发生顺序；单次最多 500 条，超出置 `eventsTruncated`），不暴露 `lastError`、下次尝试时间或管理员字段。
- 测试：`POST /api/v1/sources/{id}/test` 只发测试消息，不计入生产统计。

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
- 展示时长：默认 `8000 ms`；模板可经 `display.durationMs` 覆盖为 5000–30000，设备仍以 `message.durationMs` 上限约束。旧固件会拒收未知 `display` 字段，使用非缺省值时需先升级设备。
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


### S5-D 消息界面与已读闭环（2026-11-11）

已实现：

- `presentation/message_presenter`：纯 C 展示状态机。优先级队列、DND、critical 抢占、100 ms 刷新门控、纯文本回退、失败重试、TTL/队列移除检查、自动弹窗超时隐藏并保留未读；空闲 BOOT 可重新打开未读，手动查看不受弹窗时限约束。
- `ui_lvgl`：LVGL 9.5.0，240×320 竖屏，中文标题与正文、来源、类别、滚动进度、空态和确认失败反馈。五种模板暂共用基础文本布局，payload 中作者/文章/成长数值不在此切片；音频提醒与表情待语音底座。
- 显示/按键 HAL 与 DODO BSP：ST7789 SPI3、RGB565 分条刷新、GPIO42 PWM、GPIO0 输入。整帧传输完成且背光开启才推进 displayed。DMA 超时保留 BSP 自有缓冲区，完成前不覆盖；渲染失败熄屏并保留消息。
- BOOT 30 ms 消抖、800 ms 以上长按忽略、启动按住忽略；按下绑定画面代次，期间发生抢占/过期/黑屏则松开不确认新消息。仅接入已确认的 BOOT 引脚，触摸输入未启用。
- Noto Sans SC 16/24 px、2 bpp，6882 字符子集；字体生成脚本、来源哈希、产物哈希与 OFL 授权入库。非法 UTF-8、控制字符和缺字以方框显示，不输出消息正文到日志。
- MQTT DATA 回调通过 2 槽 PSRAM 收件交接队列转到独立任务，避免在 MQTT API 锁内写 NVS，并避免与 UI 队列锁形成反向锁等待。消息中心回执出口改为 MQTT QoS 1 enqueue；传输未接受 displayed/read 时不推进队列状态，已读失败保留消息供重试。重复投递补发队列当前状态，不倒退到 received。
- 补齐持久化边界：NVS 初始化失败不自动擦除；持久化不可用不发送 received，也不允许 UI 跳过持久化确认。网络配置在组合根中使用贯穿运行期的存储。
- `board.json` 成为显示、BOOT 和 DHT11 默认引脚来源；生成头和 Kconfig 由构建校验一致性。

验证入口：`tools/ci/test_host.sh`（展示状态机、真实 service/store/codec/storage + OS/Flash/网络边界模拟）、`tools/ci/test_ui_host.sh`（真实 LVGL 主机渲染）、`tools/ci/build_firmware.sh`。主机渲染覆盖中文、最大正文、滚动、缺字、空态与错误反馈；无需设备或 Broker，不向外发消息。

本轮结果（2026-11-11）：全部主机测试、真实 LVGL 渲染、展示状态机与消息服务的 ASan/UBSan 检查通过；契约、板级生成物和组件依赖检查通过。默认、关闭 UI、fake 网络/传感器、关闭网络四种固件构建均成功，编译无 warning/error。默认 `.bin` 为 2,822,416 B，静态 DIRAM 142,218 B（41.61%），4 MiB 应用分区余量 33%；三个变体依次为 1,041,552 B、2,016,416 B、241,680 B。字体产物 SHA-256 与 `manifest.json` 一致；构建容量不代表运行时堆峰值。

仍待：实机屏幕/按键/掉电/重连/内存与 24 h 验收；retained config、status 队列摘要、触摸、语音优先、提示音/表情和模板 payload。已读进入 MQTT outbox 后瞬间掉电的服务端收敛，需要后续持久回执与端到端测试；当前不将 enqueue 成功表述为服务端已收到。


### S5-E 持久回执与重启补发（2026-11-11）

- 收件箱、去重环与最多 16 条待发回执原子提交到同一 v2 快照。收到/显示/已读状态只有落盘成功才对调用方生效；离线已读可保存，落盘失败保留原状态，回执已满时拒绝新增状态并保留已有记录。
- 同 ID 合并至最新状态，重投不倒退；重复且状态不变不重写 Flash。TTL 清理一并持久保存 read 回执，启动恢复不提前丢掉过期记录。
- 独立补发任务每秒运行，单条 PUBACK 等待 3 s；按 MQTT packet ID 匹配确认，只确认发送队列入队不算成功。MQTT outbox 上限 4 KB；超时、离线、清理落盘失败均保留待发记录。网络等待期间不持消息锁，旧 displayed 确认不会删掉期间升级的 read。
- v2 包含完整 v1 收件箱和固定长度回执，以外层 CRC32 校验；可读旧 v1，下一次变更写 v2。旧固件不能解码 v2，降级需另行备份与迁移。每代上限 20,412 B。
- 保存前只清理非活动快照键，随后写新代并切换 active，避免 NVS 内部保留旧 blob 导致三份峰值；损坏回退后保护实际恢复的有效代，active 切换失败不改变当前代。分区不变，无实机擦写。

验证：全部主机测试通过，覆盖回执积压、掉电前后两个进程恢复、PUBACK 超时、确认后落盘失败、状态并发升级、快照容量与损坏回退；服务/存储编解码 ASan/UBSan 通过。ESP-IDF NVS 生成工具验证 64 KB 容得下两份最大快照、容不下三份，因此必须控制更新峰值。默认、关闭 UI、fake 网络/传感器、关闭网络四种构建通过；默认 `.bin` 2,824,080 B、静态 DIRAM 143,210 B（41.90%），应用分区余量 33%；三个变体分别 1,043,472 B、2,017,536 B、241,680 B。

PUBACK 语义依据 [ESP-MQTT 官方文档](https://docs.espressif.com/projects/esp-mqtt/en/latest/esp32/)。主机网络边界模拟只能验证补发状态机，不证明真实 MQTT、NVS 时序、磨损或物理掉电正确。

S5-E 当时的联调前置为 nexus-core 协议不兼容；S5-F 已完成适配，见下节。以上为历史切片记录，不代表当前仍使用旧信封。


### S5-F nexus-core v1 协议适配（2026-11-11）

- 保存服务端 v1 的版本化 Schema、样例和源码 SHA，更新 config 镜像与生成 TS；旧信封独立保留。线上固定 `/inbox`、`/acks`、`/state` 与 UUID/eventId/deviceId，不修改服务端实现。
- 有界解析 1024 B 报文，校验目标设备、精确主题、非 retained、完整字段与 UTF-8 字节限制。未对时、队列满、存储失败均不接受为 received；服务端继续重试。满队列不再挤掉已接管消息。
- v3 快照保留完整投递身份、待发状态与有界终态记录。v1/v2 旧消息仍可读，旧回执独立留档；重启、服务端重投和更换设备身份不会把旧投递伪装成新身份。TTL 清理使用 rejected/expired。
- 在线状态使用 observedAt，连接后及每 30 秒更新；LWT 与正常停机发送合法 offline。配置下发和额外状态摘要没有在服务端契约之外自行扩展。

验证结果：主机测试通过，新增身份/主题/retained/时间/过期/满队列用例；真实冻结的 v1/v2 二进制升级、故障回退、离线已读与已确认终态跨进程重启通过。解析器、存储编解码与服务故障链路的 ASan/UBSan 检查通过。

源码契约回环使用当前 Java DeliveryWorker/DeviceText 生成 593 B 含中文与转义的报文，经真实 C 服务持久化并生成 received/displayed/read/重投回执，再由当前 Java DeliveryService 接受；拒绝原因、在线/LWT 状态与 Schema 一致。Java 编译需 JDK 21；宿主默认 JAVA_HOME 指向旧版本，验证时临时选择 JDK 21，未修改全局环境。服务端源码有既存 deprecated API 提示。

ESP-IDF 官方 NVS 生成工具确认：两份 23,116 B 最大快照加当前最大凭据字符串和 28 B 温湿度存档可放入 64 KB；三份会抛 InsufficientSizeError，非活动代清理仍不可省略。该容量检查不证明动态 Flash 时序或磨损。

四种固件构建通过：默认 `.bin` **2,824,592 B**，静态 DIRAM **144,778 B（42.36%）**，应用分区剩余 **33%**；关闭 UI **1,044,016 B**，fake 网络/传感器 **2,017,648 B**，关闭网络 **241,680 B**。未新增编译器 warning；ESP-IDF 既有 Kconfig NOTE 仍存在。

本切片未烧录、未部署、未连接真实 Broker/数据库。硬件屏幕/BOOT、TLS/ACL、服务端事务、断网掉电恢复、堆水位与 24 h 稳定性仍待验收；配置下发、更多状态摘要及 AI 语音另行推进。

## 2026-11-12 消息呈现与管理接口

补齐按类型的图标/评论引用/系统样式、提示音、显示成功后发声、普通通知避让语音与 critical 抢占；静音/DND 仍优先。配置缩小 queueDepth 不删除已接收消息。远程配置与 telemetry 使用 [device-management-v1](../integration/contracts/device-management-v1/README.md)，不改 NEXUS 原 inbox/acks/state；服务端和 Broker ACL 未部署。
