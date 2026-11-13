# NEXUS v1 线协议快照

2026-11-11 按同级 `nexus-core` 当前源码固定；源码 SHA-256 见 `source-manifest.json`，不包含服务端凭据、业务数据或 Java 实现副本。

2026-11-13 核对历史原文件后更新 `MqttTransport.java` 指纹：新增 `telemetry`、`config_acks` 订阅与 `publishConfig`，并收紧主题前缀校验；原 inbox/acks/state 的主题、QoS、1024 B 上限及消息字段保持兼容。配置通道另见 `device-management-v1`。随后加入默认关闭的 `ota_acks`/`ota_status` 条件订阅及 `publishOta`，见 `remote-ota-v1`；成长状态条件订阅见 `growth-sync-v1`。这些变更未调整原消息 schema 与示例，源码变化检查继续启用。

管理台 CONSOLE-03 仅修改 `DeliveryService.list` 的可选状态读取与计数，不涉及消息信封、回执、重试与状态流转。已比对 CORE `07e3c53` 原指纹与 `b85f704` 的完整差异，仅更新该源文件指纹，其余五项保持；守卫继续拒绝未经核对的源码变化。验证结果见配套 [23](../../../docs/23-console-completion.md#console-03--消息与投递状态筛选)。

CONSOLE-04 在同级 CORE 增加 V13 状态历史、`cancelled` 和仅未开始投递可用的管理员取消。已核对从 `af916aa` 的 `DeliveryService` 基线（与原指纹一致）到本项的完整差异：新增管理读取/取消与终态 ACK 防复活；原 Worker、主题、消息/ACK 字段、QoS 和其他五项源码指纹保持。仅更新该服务源码指纹，并在实际 Java→C→Java 回环中补“取消后 ACK 不恢复”的断言。取消不产生设备撤回包，设备协议没有新增 cancelled ACK；SQL 触发器/竞争与迁移由 CORE 的真实 PostgreSQL 专项验证，不能用此边界替身回环代替。管理契约见 [CORE 文档](../../../../nexus-core/docs/console-delivery-history.md)。

- `message.schema.json`：DeliveryWorker 的 inbox.message 信封，epoch 秒、UUID、96/180 UTF-8 字节标题/摘要、整包不超过 1024 B。
- `message-ack.schema.json`：DeliveryService 接受的 received/displayed/read。
- `message-rejection.schema.json`：服务端拒绝原因集合；固件当前持久发送 expired，临时失败交给重试。
- `device-state.schema.json`：online 与 observedAt；LWT 可为 offline/0。
- `*.example.json`：固定的非敏感示例；时间仅用于契约测试，不用于实际投递。

标准 JSON Schema 的 maxLength 按字符计数；UTF-8 字节上限和 expiresAt > sentAt 由固件解析器与针对性测试补充。`config/schemas`、`config/examples` 保留相同镜像，`check_contracts.py` 校验漂移，TS 类型从镜像生成。修改协议时先核对服务端源文件并新增适当版本或更新同版的兼容快照，再更新镜像、生成物和测试。

服务端 `deliveries` 对 `(message_id, device_id)` 唯一，重试复用同一 eventId。设备必须保留完整身份；不能用旧 msg_ ID、hash 或猜测的 eventId 构造回执。

源码契约回环和真实联调边界见 [integration README](../../README.md)。
