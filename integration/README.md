# integration

与 `nexus-core` 的集成边界。只放契约、部署配置与联调说明，不放消息中心或 AI 业务代码。

## 目录

- `contracts/`：`nexus-core` 的 OpenAPI、AsyncAPI、JSON Schema 快照，按版本存放；设备消息信封与 ACK 契约以此为准。
- `deploy/`：Broker、`nexus-core`、`ai-gateway` 的 compose 与 env 示例（不含凭据）。

## 边界

- 消息中心归 `nexus-core`：Webhook/Halo、去重与保留、设备注册、MQTT ACL、配置下发、管理台。
- AI 网关归 [`../server/ai-gateway/`](../server/ai-gateway/)：音频会话与供应商适配。
- `nexus-core` 不代理音频流；`ai-gateway` 不直连设备数据库。
- 契约变更先更新本目录快照并升版本，再改消费方；不在 `04` 内复制 Java/Vue 实现。
