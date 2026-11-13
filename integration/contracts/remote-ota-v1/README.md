# remote-ota-v1

2026-11-13，独立于原 nexus-v1 消息和 device-management-v1 配置通道。字段事实源为 config/schemas，C/TS 生成物仍由 schema_codegen 生成。完整事务、HTTP、启用与验收边界见 [远程 OTA](../../../docs/19-remote-ota.md)。

所有主题前缀为 `nexus/v1/devices/<deviceId>/`，QoS1，**全部禁止 retained**：

| 主题 | 方向 | 契约与大小 |
| --- | --- | --- |
| ota | CORE→设备 | [PetOtaCommand](../../../config/schemas/ota-command.schema.json)，≤1024 UTF-8 B |
| ota_acks | 设备→CORE | [PetOtaAck](../../../config/schemas/ota-ack.schema.json)，≤512 B |
| ota_status | 设备→CORE | [PetOtaStatus](../../../config/schemas/ota-status.schema.json)，≤1536 B，每约 5 秒 |

命令包含 schemaVersion=1、deviceId、安装 requestId、独立 commandId、operation=install/reboot、当前 bootId、issuedAt/expiresAt，以及固定目标 releaseId/version/imageSha256/imageSize/releaseSequence。安装 commandId 必须等于 requestId；重启必须不同。ID 仅字母/数字/下划线/连字符；设备/请求 ID ≤40，发布目录 ≤64，版本 ≤32，SHA256 为小写 64 hex。bootId 为随机 32 hex。

UTC 秒非零、expiresAt>issuedAt、差值≤300，未来 issuedAt、未对时和到期新命令拒绝。相同 bootId/已记录 ID/完全相同目标的命令可回答既有结果，不重复动作；旧 bootId 永不执行。未知/重复字段、转义 NUL、非整数和过深输入拒绝。

ACK 固定 schemaVersion/requestId/commandId/operation/accepted/status/code。无法安全解析身份时 ID 或 operation 可为空，此类回执不关联 CORE 命令。accepted 仅为持久接受，status=accepted 且 code=0；其他 status 为 rejected/expired/conflict/unavailable，code 非零。有效回执必须与已尝试下发的命令、请求、操作一致；不通过 ACK 推导安装成功。

完整状态含 schemaVersion/deviceId/observedAt/reportSequence/bootId/available/state/code/requestId/rebootId/writtenBytes/reservedSequence/identity，以及有账本时的 target。identity 为 project、board、modelSha256、partitionSha256、configSchema、growthSchema；modelSha256 可以是 none。requestId 为空时无 target。available 只决定是否可接收新动作，不代表在线；原 state/LWT 仍拥有在线语义。

同启动 reportSequence 递增，时间不倒退；新启动可从 1 开始，旧启动重现会被持久历史拒绝。观察时间超过 90 秒不能触发新动作，未来超过 5 秒拒绝。新镜像 healthy/verifying 必须来自新启动，且 target 完整匹配。旧状态、旧 ACK、retained 和跨设备报文不能确认新请求。

JSON Schema 验证字段类型/范围；时间关系、UTF-8 字节数、操作关联、状态一致性、持久化和动作去重由真实 Java/C 解析器和回归补充。示例时间仅用于契约校验，不用于真实下发。
