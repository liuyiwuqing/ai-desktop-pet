# integration

与 `nexus-core` 的集成边界。只放契约、部署配置与联调说明，不放消息中心或 AI 业务代码。

## 目录

- `contracts/`：`nexus-core` 的 OpenAPI、AsyncAPI、JSON Schema 快照，按版本存放；设备消息信封与 ACK 契约以此为准。
- `deploy/`：集成部署预留；当前可用的 CORE 服务/TLS 代理模板与恢复步骤在 [`../deployment/`](../deployment/README.md)，数据库/Broker 沿用同级 `nexus-core/compose.yml`。

## 边界

- 消息中心归 `nexus-core`：Webhook/Halo、去重与保留、设备注册、MQTT ACL、配置下发、管理台。
- 自建 AI 网关预留在 [`../server/ai-gateway/`](../server/ai-gateway/)；当前 ESP32 直接连接小智，协议与联调见 [12](../docs/12-cloud-interop.md)。
- `nexus-core` 不代理音频流；`ai-gateway` 不直连设备数据库。
- 可选的公开记忆工具由 CORE MCP 与 stdio 适配提供，凭据只授权固定检索工具，不下发设备；契约见 [memory-tools-v1](contracts/memory-tools-v1/README.md)，接口/真实验收见 [16](../docs/16-voice-memory-tools.md)，固定桥接、受控启动与实际 WSS→stdio→CORE 的本地回归见 [17](../docs/17-voice-bridge-regression.md)。
- 契约变更先更新本目录快照并升版本，再改消费方；不在 `04` 内复制 Java/Vue 实现。

## S5-F 当前契约（2026-11-11）

固件已适配现有 `nexus-core` v1。契约快照、示例和服务端源码 SHA-256 位于 [`contracts/nexus-v1/`](contracts/nexus-v1/README.md)；`config/schemas` 镜像与生成 TS 由 CI 检查一致性。旧本地格式保留在 `contracts/legacy-device-v1/`，仅供迁移与历史对照。

| 内容 | 当前协议 |
| --- | --- |
| 收件 | `/inbox`；`type=inbox.message`；顶层 eventId/deviceId/sentAt/expiresAt，消息位于 payload |
| 回执 | `/acks`；schemaVersion/deviceId/eventId/messageId/status；过期使用 rejected/expired |
| 在线状态 | `/state`；schemaVersion/deviceId/online/observedAt；retained 在线快照、30 秒更新、offline LWT |
| 身份与容量 | 小写标准 UUID；deviceId 为 `[a-zA-Z0-9_-]{1,40}`；包 ≤1024 UTF-8 字节，标题 ≤96 字节、摘要 ≤180 字节 |
| 配置下发 | device-management-v1 已接通 CORE、管理台与设备服务的隔离回环，真实部署待做 |

### 源码契约回环

```bash
JAVA_HOME="$(/usr/libexec/java_home -v 21)" python3 tools/ci/test_nexus_contract.py
```

在 `04` 根目录执行。默认读取同级 `nexus-core`；可用 `--nexus-root` 指定位置，`--runtime-jar` 指定已有 Spring Boot JAR。依赖 JDK 21 和 C 编译器，不安装依赖、不修改服务端。

测试编译当前服务端 `DeliveryWorker/DeviceText/DeliveryService` 源码：Java 生成含中文、emoji、转义和裁剪边界的信封，C 的真实消息服务执行收件、NVS 事务模拟、displayed/read、终态重投，再由 Java 服务校验回执、过期原因与在线/离线状态。报文还必须通过本仓 Schema。源文件与快照 SHA 不一致会失败，要求先核对契约。

SQL、网络和 Flash 驱动在测试中使用明确的边界替身；没有连接 Broker 或数据库。这证明源码协议兼容，不替代 MQTT/TLS、ACL、真实事务、硬件掉电和长期稳定性验收。

### 数据升级

v3 快照保存完整 eventId/deviceId。v1/v2 收件箱继续可读，旧 msg_ 回执独立留档，不编造服务端投递身份。已确认 read/expired 记录有界保留，用于服务端重投；未确认回执不被新消息覆盖。更换 deviceId 后旧回执保持原身份并暂停发送。旧固件不能读取 v3，不能直接回刷降级后继续使用这份存档。

2026-11-12：新增 [device-management-v1](contracts/device-management-v1/README.md) 远程配置、遥测、完整设置回读与状态审计，原 state 契约保持不变。CORE 持久事务、ACL 升级工具及管理台已实现，Java HTTP/PostgreSQL/MQTT→真实 C 配置服务→ACK 的隔离回环通过：

```sh
python3 tools/ci/test_management_contract.py
```

该入口使用独立容器和测试凭据，C 的 OS/Flash 驱动在主机托管；没有升级真实库/ACL 或操作 ESP32。当前验证与部署边界见 [11](../docs/11-product-software-closure.md)。
