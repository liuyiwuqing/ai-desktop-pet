# server

独立 AI 网关。消息中心与第三方消息入口不在这里，归工作区现有 `nexus-core`。

## 目录

- `ai-gateway/`：AI 会话服务。承载 ASR/LLM/TTS 编排、供应商适配、上下文策略与密钥管理；对设备暴露 WSS/HTTPS。

## 边界

- 不实现消息接入、去重、ACK、配置下发与管理台；这些由 `nexus-core` 负责。
- 不直连设备数据库；设备身份由 `nexus-core` 校验，AI 网关只校验短期会话令牌。
- 供应商密钥只放本服务私有 env，不下发设备、不进仓库。
- 首版可复用 `03-xiaozhi-voice` 的小智 WebSocket 服务作为实现；对外仍以 `ai_session` 契约暴露，后续替换实现不改固件。
- 与 `nexus-core` 的接口契约、部署配置放在 [`../integration/`](../integration/)。
- 详细方案见 [`../docs/01-ai-agent.md`](../docs/01-ai-agent.md) 与 [`../docs/02-message-hub.md`](../docs/02-message-hub.md)。
