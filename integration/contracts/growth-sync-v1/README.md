# growth-sync-v1

设备只读成长快照的独立 MQTT 扩展，详见 [21](../../../docs/21-growth-sync.md)。

主题 `nexus/v1/devices/{deviceId}/growth_status`，QoS 1、非 retained、最大 1536 UTF-8 B；设备只发自己的主题，CORE 仅读取。消息包含 `schemaVersion=1`、`growthSchema=4`、设备、随机启动标识、启动内递增序号、Unix 秒与成长视图。

正式 Schema/示例分别在 [Schema](../../../config/schemas/growth-status.schema.json) 和 [示例](../../../config/examples/growth-status.example.json)，TypeScript 由同目录上级 generated 生成。等级与经验门槛、领取位与任务进度的交叉约束由实际 C formatter 和 Java ingress 校验，通用 Schema 只表达字段范围。

只接受同启动的更大序号，时间不倒退；曾退役的启动不再成为当前启动。重复/retained 报告不延长新鲜度，90 秒后标记过期。`unsaved` 是 RAM 变化标志；镜像不作为恢复包、不触发奖励、不更新在线心跳，不改变既有 inbox/state/config/OTA 协议。

实际 C→MQTT→CORE 检查：`python3 tools/ci/test_growth_sync_contract.py`。主机引擎/JSON 与真实 Java、PostgreSQL、MQTT 已验证，Flash、Wi-Fi、TLS 和实机调度需另验。
