# device-management-v1

2026-11-12，设备、`nexus-core` 和管理台已实现版本 1，并完成隔离联调；真实数据库、Broker ACL 和设备尚未升级。与 `nexus-v1` 消息快照并列，原 inbox/acks/state 契约不变。

所有主题前缀 `nexus/v1/devices/<deviceId>/`。设备账号名与 deviceId 一致，ACL 只允许其自身主题；CORE 持有配置发布权限。管理员 REST 仍使用现有 Bearer 令牌，没有新增多用户/RBAC。

| 主题 | 方向/保留 | 内容 |
| --- | --- | --- |
| config | CORE→设备，QoS1，禁止 retained | [完整配置信封](../../../config/schemas/config-request.schema.json)，≤1200 UTF-8 字节 |
| config_acks | 设备→CORE，QoS1，非 retained | [本地事务结果](../../../config/schemas/config-result.schema.json)，设备身份取受 ACL 保护的主题 |
| telemetry | 设备→CORE，retained，约 30 秒 | [遥测](../../../config/schemas/telemetry.schema.json)，≤2048 字节；可附完整非敏感 config |

## 设备事务

配置信封严格七个字段，未知/重复字段拒绝：

| 字段 | 约束 |
| --- | --- |
| schemaVersion | 整数 1 |
| deviceId | 当前设备 ID，`[a-zA-Z0-9_-]{1,40}` |
| requestId | `[a-zA-Z0-9_-]{1,40}`，用于关联 |
| expectedRevision | 0–4294967294 整数 |
| issuedAt | 非零 UTC 秒，不能晚于设备已同步时间 |
| expiresAt | issuedAt 之后且差值 ≤300 秒，当前时间必须小于此值 |
| config | [完整 DeviceConfig](../../../config/schemas/device-config.schema.json)，无凭据 |

未对时、retained、错设备、过期或非法配置都拒绝。语言仅 zh-CN，时区为 Asia/Shanghai、UTC、Etc/UTC 或 UTC±HH:MM（UTC−12 至 UTC+14，不自动切换夏令时）。成功必须经过 NVS 回读；相同完整配置的无操作成功不写盘。正常 CAS 只接受当前 revision；重投若当前 revision=expectedRevision+1 且规范配置完全一致也返回 applied。requestId 不是设备永久动作去重账本，设备幂等依靠配置 CAS 与内容一致性。

回复固定 schemaVersion/requestId/status/revision/code；status 为 applied、rejected、expired、conflict_or_storage。无法安全解析 requestId 时为空；code 为 ESP 错误值，0 才是成功。revision 为当时可读版本，读取失败为 0。ACK 发送失败不撤销已提交设置。配置 applied 只表示本地存储事务完成，不表示屏幕、声音或物理静音已经验收。

本机设置经后台存储拥有者保存；入队前预留配置版本，远程旧版本在本地事务结束前不能穿插覆盖。入队失败释放预留；存储失败时本机隐私选择仍保持运行期门控并显示未保存。没有在 UI 任务直接写闪存。

## CORE 管理接口

路径前缀 `/api/v1/devices/<deviceId>/management`，沿用管理员 Bearer 鉴权；遥测历史位于同级 `/telemetry`：

| 接口 | 行为 |
| --- | --- |
| GET `/management` | 遥测、fresh、canConfigure/configBlock、最近 25 条配置记录及请求内容 |
| GET `/telemetry` | 只读历史；`from`/`to`（ISO-8601）与 `bucket=raw\|1m\|5m\|15m\|1h\|1d`，默认最近 6 小时、5 分钟桶 |
| POST `/management/config-requests` | `{requestId, expectedRevision, config}`；首次 202，同 ID 同内容 200，异内容/版本冲突 409 |
| GET `/management/config-requests/<requestId>/audit` | 最多 100 条 admin/core/device 状态审计，不保存令牌 |

历史样本不是新的设备主题：它由既有 retained `telemetry` 消息追加，变化即记（5 秒下限）、稳定期 30 秒一条、保留 30 天；`raw` 窗口最多 12 小时和 2000 点，聚合桶最多约 2000 点。空桶不补零，最后一条历史不代表设备在线。字段语义与配置见 CORE 的 [遥测历史](../../../../nexus-core/docs/device-telemetry-history.md)。

必须有 90 秒内的完整、可写配置回读才允许新提交。相同时间戳只接受更高 revision，旧遥测不能覆盖新观察；未来超过 5 秒拒绝。retained 不是在线证明，设备 online/last_seen 仍只由原 state/LWT 更新。devices.enabled 继续只控制新消息投递，不被扩展成配置授权开关。

每设备同时最多一个 pending/sent/unconfirmed 请求。CORE 在网络发送前持久化尝试预留，默认每 15 秒重投同一 requestId/CAS/到期时间。没有盲目增加 expectedRevision。收到 applied ACK 时还要检查 code=0、revision 为 expected 或 expected+1；更新后的完整设置尚未回传前，阻止再次提交。

| CORE 状态 | 含义 |
| --- | --- |
| pending | 已持久化，未开始下发 |
| sent | 已预留发送尝试，等待设备结果；不等于已送达 |
| applied | 有有效 ACK 或匹配配置回读证据 |
| rejected | 设备拒绝，或回读显示配置被更新成其他内容 |
| expired | 从未尝试发送就到期，设备明确到期，或到期后回读证明未应用 |
| unconfirmed | 曾尝试下发但到期仍无结果，阻止创建新请求；允许晚到 ACK 或新鲜回读核对 |

回读核对要求观察时间不早于 issuedAt。配置相同且 revision=expected/expected+1 才能确认；更高且内容不同记冲突。未知结果不能通过换 ID 或提高版本强行重发。终态不回退。HTTP 提交结果未知时，管理台保留原请求，刷新记录或“重试同一请求”；轮询不会覆盖编辑中的草稿，设备版本变化时要求重新加载。

## 升级与验证

设备显示名称单独使用 `PATCH /api/v1/devices/<deviceId>/name`，提交 `{name,expectedName}`。这是 CORE 元数据接口，沿用管理员鉴权及旧名称冲突检查，不发布 MQTT、不改变配置 revision/设备身份，不新增 Flyway 迁移。当前实现和回归见 [14](../../../docs/14-recovery-and-device-names.md)。

Flyway V8、`nexus-core/tools/broker_policy.py` 新 ACL 和包限制须成套升级。已有实例使用 `tools/upgrade_broker_policy.py` 预览，显式 --apply 才备份并写入；脚本不重启 Broker，拒绝覆盖自定义规则。当前未执行真实迁移或 ACL 更新，见 [部署准备](../../../deployment/README.md)。

```sh
bash tools/ci/test_config_host.sh
python3 tools/ci/test_management_contract.py
# nexus-core 中执行完整 HTTP/PostgreSQL/MQTT/ACL 集成检查
python3 tools/run_local.py test
```

`test_management_contract.py` 运行真实 Java HTTP/PostgreSQL/MQTT→本仓 C 配置服务→ACK 回环，C 端只在 OS/Flash 边界托管。集成测试另覆盖新鲜度、只读、幂等/CAS、到期未知、回读核对、非法 JSON/ACK、跨设备主题隔离；这些证据不等于真实 ESP32 NVS 掉电、生产 TLS、物理输出或云端验收。
